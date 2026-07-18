# CoinStatsIndex — Coinstake Awareness

## History

### Original Problem (July 1, 2026)

The CoinStatsIndex was not coinstake-aware. It tracked `fCoinBase` (coinbase flag) in the muhash and stats, but not `fCoinStake` (coinstake flag). This meant:

- The muhash couldn't distinguish coinstake outputs from regular outputs
- The stats didn't track coinstake reward totals separately
- The RPC didn't expose coinstake-specific information

### Failed Attempt: `002b58d84f` "index: make coinstatsindex coinstake compatible" (July 1, 2026)

**What was attempted:**
1. Added `fCoinStake` and `nTime` to the muhash via `TxOutSer` in `kernel/coinstats.cpp`
2. Added `nBlockTime` parameter to `AddCoins` to set `coin.nTime = nBlockTime` for v2 txs
3. Added undo data recovery code in `coinstatsindex.cpp` to handle old undo data with `nTime = 0`
4. Threaded `nBlockTime` through `UpdateCoins`, `ConnectBlock`, `RollforwardBlock`

**Why it failed:**
1. The muhash included `nTime`, which was `block.nTime` for new outputs but `0` in old undo data → muhash drifted → assertion crash on reorg
2. The `AddCoins` change made `coin.nTime = nBlockTime` for v2, which activated the latent `coin.nTime > nTimeTx` check in `CheckTxInputs` → block 5944947 rejected with `bad-txns-time-earlier-than-input`
3. The undo data recovery code used `GetAncestor(coin.nHeight)` in the inner loop → O(n²) performance

### Failed Fix: `a52b290c1d` "block: mainnet height 5944947 fix" (July 9, 2026)

Added `nBlockTime` parameter to `CheckTxInputs` to make the time check symmetric with `AddCoins`. This fixed the block rejection but introduced v1/v2 asymmetries in the mempool path.

### Failed Fix: `c2455cdd6f` "block: mainnet temp fix" (July 15, 2026)

Refined the `CheckTxInputs` fix with proper mempool handling (using `chain.Tip()->GetBlockTime()` instead of wall clock).

### Final Resolution: Full Revert + Clean Coinstake Awareness (July 17, 2026)

**Approach:** Revert all three failed commits and add coinstake awareness without touching consensus code.

**Key insight:** The muhash does NOT need to include `nTime` to be coinstake-aware. The `fCoinStake` flag is a structural property of the transaction (determined by `tx.IsCoinStake()`, which checks `vin.size() > 0 && !vin[0].prevout.IsNull() && vout.size() >= 2 && vout[0].IsEmpty()`). It's the same value whether the tx is read from disk or memory, so the muhash is deterministic without any `nBlockTime` threading.

## Current Implementation

### What was reverted (back to v262 behavior)

| File | Change |
|---|---|
| `coins.cpp` | `AddCoins` uses `tx.nTime` directly (no `nBlockTime` parameter) |
| `coins.h` | `AddCoins` signature: 4 args, no `nBlockTime` default |
| `consensus/tx_verify.cpp` | `CheckTxInputs` uses `GetAdjustedTimeSeconds()` for v2 (v262 behavior) |
| `consensus/tx_verify.h` | `CheckTxInputs` signature: 5 args, no `nBlockTime` |
| `validation.cpp` | `UpdateCoins` no `nBlockTime` param; `ConnectBlock`/`RollforwardBlock`/`PreChecks` callers reverted |
| `txmempool.cpp` | `CheckTxInputs` and `AddCoins` callers reverted |
| `test/coins_tests.cpp` | Regression tests for v284 `nBlockTime` behavior removed |
| `test/fuzz/coins_view.cpp` | `CheckTxInputs` caller reverted; `util/time.h` include removed |

### What was added (coinstake awareness)

#### 1. Muhash encoding (`kernel/coinstats.cpp`)

`TxOutSer` now encodes `fCoinBase` in bit 0 and `fCoinStake` in bit 1, with height in the upper bits:

```cpp
ss << static_cast<uint32_t>((coin.nHeight << 2) | (coin.fCoinBase ? 1u : 0u) | (coin.fCoinStake ? 2u : 0u));
```

**`nTime` is NOT included in the muhash.** This is the key design decision that avoids all the `nBlockTime` threading complexity. The muhash only depends on:
- `outpoint` (txid + vout index)
- `nHeight | fCoinBase | fCoinStake` (height + 2 flag bits)
- `out` (amount + scriptPubKey)

For PoW-only chains, `fCoinStake` is always 0, so the encoding is compatible (just shifted by 1 bit from the previous `(nHeight << 1) | fCoinBase`).

#### 2. Stats tracking

| Location | Field | Purpose |
|---|---|---|
| `kernel/coinstats.h` | `CCoinsStats::total_coinstake_amount` | `std::optional<CAmount>` — total coinstake reward value |
| `index/coinstatsindex.h` | `m_total_coinstake_amount` | `CAmount` — index's running total |
| `index/coinstatsindex.cpp` | `DBVal::total_coinstake_amount` | Serialized in the index DB |

#### 3. Index logic (`index/coinstatsindex.cpp`)

- `CustomAppend` and `ReverseBlock` now distinguish three categories:
  - `tx->IsCoinBase()` → coinbase output
  - `tx->IsCoinStake()` → coinstake output (PoS staking reward)
  - otherwise → regular output
- The `unclaimed_rewards` formula now includes `m_total_coinstake_amount` on the outputs side (coinstake rewards are the staker's reward, not unclaimed)
- `LookUpStats` and `CustomInit` populate the new field
- Consistency `Assert` added in `ReverseBlock`

#### 4. Direct path (`kernel/coinstats.cpp::ApplyStats`)

The direct (non-index) `ComputeUTXOStats` path also tracks `total_coinstake_amount` using the per-coin `fCoinStake` flag. Uses `CheckedAdd` for overflow safety, same as `total_amount`.

#### 5. RPC output (`rpc/blockchain.cpp`)

New `coinstake` field in `gettxoutsetinfo` `block_info`:

```json
{
  "block_info": {
    "prevout_spent": ...,
    "coinbase": ...,
    "coinstake": ...,
    "new_outputs_ex_coinbase": ...,
    "unspendable": ...
  }
}
```

#### 6. Tests (`test/coinstatsindex_tests.cpp`)

New test `coinstatsindex_coinstake_awareness` verifies:
- The 2-bit encoding scheme (fCoinBase=bit0, fCoinStake=bit1)
- All three categories produce different encoded values
- `GetBogoSize` is unchanged in size

## Design Properties

### No consensus changes

The consensus path is completely untouched:
- `AddCoins` uses `tx.nTime` (v262 behavior)
- `CheckTxInputs` uses `GetAdjustedTimeSeconds()` for v2 (v262 behavior)
- The `coin.nTime > nTimeTx` check is dead code for v2 (`0 > wall_clock = false`)
- No `nBlockTime` parameter anywhere in the consensus path

### No assertion crash on reorg

The muhash does NOT include `nTime`, so there's no mismatch between add and remove operations. The `fCoinStake` flag is a structural property (same value whether tx is read from disk or memory), so the muhash is consistent.

### Deterministic muhash

The muhash is deterministic across nodes because:
- `fCoinStake` comes from `tx->IsCoinStake()`, which is structural (not time-dependent)
- `nTime` is not in the muhash at all
- The same UTXO set produces the same muhash on all nodes

### v1/v2 handling

- **v1 coinstake** (legacy): `Coin.nTime = real nTime` → stored in the `Coin` object but NOT in the muhash
- **v2 coinstake** (modern): `Coin.nTime = 0` (forced by deserialize) → stored in the `Coin` object but NOT in the muhash
- The `fCoinStake` flag is set correctly for both versions

### DB format break

Adding `total_coinstake_amount` to `DBVal` shifts byte offsets of subsequent fields. Existing coinstatsindex DBs must be rebuilt. This is acceptable because all nodes upgrade together (no migration needed).

## Comparison with v262

| Aspect | v262 | Current (v284) |
|---|---|---|
| Muhash includes `fCoinStake`? | No | **Yes** (bit 1) |
| Muhash includes `nTime`? | No | No |
| Stats track coinstake? | No | **Yes** |
| RPC exposes coinstake? | No | **Yes** |
| `AddCoins` has `nBlockTime`? | No | No |
| `CheckTxInputs` has `nBlockTime`? | No | No |
| Consensus changes? | N/A | **None** |
| Requires reindex? | N/A | **Yes** (DB format change) |

## Comparison with Bitcoin upstream

| Aspect | Bitcoin | Current (v284) |
|---|---|---|
| `Coin` has `nTime`? | No | Yes (Blackcoin PoS) |
| `Coin` has `fCoinStake`? | No | Yes (Blackcoin PoS) |
| Muhash includes `fCoinStake`? | N/A | Yes |
| Muhash includes `nTime`? | N/A | No |
| Time check in `CheckTxInputs`? | No | Yes (Blackcoin PoS, dead code for v2) |
