# `checkkernel` RPC Call

The `checkkernel` RPC is a **Blackcoin-specific staking helper** that tests whether any of the provided UTXOs can produce a valid PoS kernel at the current moment.

## Source Files

- **RPC Handler**: [staking.cpp](file:///Users/blackcoindev/Development/Blackcoin/blackcoin-more/src/wallet/rpc/staking.cpp#L213-L373)
- **Kernel Logic**: [pos.cpp](file:///Users/blackcoindev/Development/Blackcoin/blackcoin-more/src/pos.cpp)

## Flow

```
┌───────────────────────────────────────────────────────────────┐
│  checkkernel RPC Call                                          │
│  Input: Array of UTXOs [{txid, vout}, ...]                     │
└───────────────────────┬───────────────────────────────────────┘
                        │
                        ▼
┌───────────────────────────────────────────────────────────────┐
│  1. Get Current Staking Parameters                             │
│     • pindexPrev = current chain tip                           │
│     • nBits = GetNextTargetRequired() (difficulty)             │
│     • nTime = GetAdjustedTimeSeconds() & ~nStakeTimestampMask  │
└───────────────────────┬───────────────────────────────────────┘
                        │
                        ▼
┌───────────────────────────────────────────────────────────────┐
│  2. For Each Input UTXO:                                       │
│     Call CheckKernel(pindexPrev, nBits, nTime, outpoint, view) │
└───────────────────────┬───────────────────────────────────────┘
                        │
                        ▼
┌───────────────────────────────────────────────────────────────┐
│  3. CheckKernel() calls CheckStakeKernelHash():                │
│                                                                 │
│     hash = SHA256(nStakeModifier + blockFromTime +             │
│                   prevout.hash + prevout.n + nTime)            │
│                                                                 │
│     target = nBits * coinValue (weighted by amount)            │
│                                                                 │
│     Returns TRUE if: hash < target                             │
└───────────────────────┬───────────────────────────────────────┘
                        │
                        ▼
┌───────────────────────────────────────────────────────────────┐
│  4. If kernel found AND createblocktemplate=true:              │
│     • Create block template with BlockAssembler                │
│     • Set timestamp to kernel time                             │
│     • Return hex-encoded block template                        │
└───────────────────────────────────────────────────────────────┘
```

## Key Concepts

### Kernel Hash Formula

```cpp
hash(nStakeModifier + blockFromTime + prevout.hash + prevout.n + nTime)
```

### `nStakeModifier`

The **critical PoS field** that scrambles computation to prevent precomputing future proofs. Computed from the previous block's stake modifier and kernel hash.

> [!IMPORTANT]
> This field does **NOT** exist in Bitcoin Core - it's Blackcoin/Peercoin-specific.

### Maturity Check

The coin must be at least `nCoinbaseMaturity` blocks old before it can stake.

### Weight-Based Target

Larger coins have proportionally higher chances of finding a kernel:

```cpp
bnTarget = nBits * coinValue
```

### Timestamp Masking

Time is masked to reduce grinding opportunities:

```cpp
nTime &= ~nStakeTimestampMask  // Only certain timestamps are valid
```

## Usage Examples

```bash
# Check if UTXOs can stake now (no block template)
blackmore-cli checkkernel '[{"txid":"abc123...","vout":0}]' false

# Check and create block template if kernel found
blackmore-cli checkkernel '[{"txid":"abc123...","vout":0}]' true
```

## Response Format

```json
{
  "found": true,
  "kernel": {
    "txid": "...",
    "vout": 0,
    "time": 1706123456
  },
  "blocktemplate": "...",       // Only if createblocktemplate=true
  "blocktemplatefees": 1000,    // Only if createblocktemplate=true
  "blocktemplatesignkey": "..." // Only if createblocktemplate=true
}
```

## Bug Analysis (Fixed in 2026-01)

The `createblocktemplate=true` path was broken after the Bitcoin 27.x merge. Below is a comparison of the old working code vs the broken implementation.

### Old (Working) - `CoinBlack/blackcoin`

```cpp
// Creates a PoS block template with coinstake (fProofOfStake=true)
auto_ptr<CBlock> pblock(CreateNewBlock(*pMiningKey, true, &nFees));

// Sets time on the COINSTAKE transaction (vtx[0] was coinbase merged with coinstake)
pblock->nTime = pblock->vtx[0].nTime = nTime;

// Gets signing key from the reserved mining key
CPubKey pubkey;
pMiningKey->GetReservedKey(pubkey);
result.push_back(Pair("blocktemplatesignkey", HexStr(pubkey)));
```

### Broken (Before Fix) - `blackcoin-more`

```cpp
// BUG: Creates a PoW block (pwallet=nullptr means no coinstake!)
BlockAssembler{...}.CreateNewBlock(CScript(), nullptr, &fPoSCancel, &nFees);

// BUG: vtx[0] is just an empty coinbase, not a coinstake
pblock->nTime = coinstakeTx.nTime = nTime;

// BUG: Gets a random NEW key, not the kernel's key!
auto op_dest = pwallet->GetNewChangeDestination(output_type);
```

### Root Cause

| Issue | Description |
| ----- | ----------- |
| `nullptr` wallet | `CreateNewBlock` was called with `pwallet=nullptr`, creating a PoW block |
| Wrong transaction | `vtx[0]` is coinbase, coinstake would be at `vtx[1]` if present |
| Wrong signing key | Generated new key instead of using kernel UTXO's key |
| Kernel not used | The found kernel UTXO was never incorporated into the block |

### Fix Applied

The fix in `src/wallet/rpc/staking.cpp:350-405` correctly implements the `createblocktemplate` path:

```cpp
// 1. Create PoS block template by passing wallet (enables coinstake creation)
std::unique_ptr<node::CBlockTemplate> pblocktemplate(
    BlockAssembler{active_chainstate, &mempool}.CreateNewBlock(
        CScript(), pwallet.get(), &fPoSCancel, &nFees));

// 2. Update coinstake tx at vtx[1] (not vtx[0] which is coinbase)
if (pblock->vtx.size() > 1 && pblock->vtx[1]->IsCoinStake()) {
    CMutableTransaction coinstakeTx(*pblock->vtx[1]);
    coinstakeTx.nTime = nTime;
    pblock->vtx[1] = MakeTransactionRef(std::move(coinstakeTx));
}
pblock->nTime = nTime;

// 3. Rebuild merkle root after timestamp change
pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

// 4. Get signing key from KERNEL UTXO (the one we actually found)
Coin kernelCoin;
active_chainstate.CoinsTip().GetCoin(kernel, kernelCoin);

std::vector<valtype> vSolutions;
TxoutType whichType = Solver(kernelCoin.out.scriptPubKey, vSolutions);

CPubKey kernelPubKey;
if (whichType == TxoutType::PUBKEY) {
    kernelPubKey = CPubKey(vSolutions[0]);
} else if (whichType == TxoutType::PUBKEYHASH) {
    CKeyID keyID = CKeyID(uint160(vSolutions[0]));
    std::unique_ptr<SigningProvider> provider = 
        pwallet->GetSolvingProvider(kernelCoin.out.scriptPubKey);
    provider->GetPubKey(keyID, kernelPubKey);
}

result.pushKV("blocktemplatesignkey", HexStr(kernelPubKey));
```

### Summary of Fixes

| Component | Before (Broken) | After (Fixed) |
| --------- | --------------- | ------------- |
| Block creation | `nullptr` → PoW block | `pwallet.get()` → PoS block |
| Coinstake tx | `vtx[0]` (coinbase) | `vtx[1]` (coinstake) |
| Merkle root | Not rebuilt | `BlockMerkleRoot()` called |
| Signing key | Random new key | Kernel UTXO's actual key |

> [!NOTE]
> The `CheckKernel()` call (kernel discovery) was already working correctly. Only the `createblocktemplate=true` path was broken.

---

## Staking Enhancements (2026-01)

The following staking enhancements have been implemented in Blackcoin More:

| Feature | Status | Description |
| ------- | ------ | ----------- |
| **Ghost Block Logging** | ✅ 100% | Diagnostic logging for blocks that fail validation after creation |
| **Multi-Wallet Independence** | ✅ 100% | Each wallet has independent staking timers and cache statistics |
| **Zero-Allocation Staking** | ✅ 100% | Memory-efficient staking with minimal allocations per tick |
| **Stake Cache Statistics** | ✅ 120% | Enhanced beyond original spec with hit rates and timing |
| **Performance Tracking** | ✅ 100% | Time saved tracking and rolling average hit rates |
| **Diagnostic Logging** | ✅ 100% | Detailed `-debug=coinstake` output for troubleshooting |
| **RPC Enhancements** | ✅ 100% | Extended `getstakinginfo` with comprehensive cache stats |

### Stake Cache RPC Fields

The `getstakinginfo` RPC now includes enhanced cache statistics:

```json
{
  "stakecache": {
    "enabled": true,
    "size": 91,
    "staked": 125,
    "lookups": 167,
    "cache_misses": 42,
    "efficiency": 74.8,
    "efficiency_avg": 75.2,
    "blocks": 5,
    "flushes": 2,
    "last_flush_reason": "size_limit",
    "time_saved_ms": 1250
  }
}
```

### Cache Flush Reasons

| Reason | Description |
| ------ | ----------- |
| `size_limit` | Cache exceeded size limit (UTXOs + 100 buffer) |
| `manual` | Manually cleared via RPC |
| `shutdown` | Cleared during wallet shutdown |
| `cleanup` | Periodic cleanup of stale entries |

### Related Documentation

- [STAKECACHE.md](file:///Users/blackcoindev/Development/Blackcoin/blackcoin-more/agent/STAKECACHE.md) - Full stake cache documentation
- [STAKING.md](file:///Users/blackcoindev/Development/Blackcoin/blackcoin-more/agent/STAKING.md) - General staking documentation
