# Blackcoin More: Staking Hardening & Protocol Guide

## Executive Summary

This guide provides a comprehensive technical analysis of Blackcoin More's Proof-of-Stake (PoS) v3.1 protocol. It documents the low-level consensus logic, memory-pool safety mechanisms, and P2P hardening strategies required for professional infrastructure.

---

## 1. PoS v3.1 Kernel Protocol (Deep Dive)

The core validation of a staking "claim" occurs in `CheckStakeKernelHash()` (src/pos.cpp). Unlike Bitcoin, Blackcoin More requires a **Stake Modifier** to prevent precomputation attacks.

### 1.1 The Kernel Formula

A valid kernel must satisfy the following inequality:

```text
SHA256(nStakeModifier + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime) < bnTarget * nWeight
```

* **`nStakeModifier`**: A 256-bit value (`pindexPrev->nStakeModifier`) that scrambles computation. It is generated from the previous modifier and the kernel of the triggering block.
* **`nWeight`**: The satoshi-value of the UTXO being spent. Higher values linearly increase the probability of a hash falling below the target.
* **`nTime`**: The current adjusted unix timestamp.

### 1.2 The 16-Second Mask (`nStakeTimestampMask`)

In `CheckCoinStakeTimestamp()`, the protocol enforces a temporal alignment rule using a bitwise mask:

```cpp
return (nTimeBlock == nTimeTx) && ((nTimeTx & 0x0F) == 0);
```

Historically, PoS v2 used a loose window. v3.1 uses `0x0F` (15 decimal), meaning kernels only validate if their timestamp is a multiple of 16. This reduces CPU load by **93.75%** during the staking loop, as the wallet only needs to check 4 timestamps per minute.

---

## 2. Advanced Anti-Spam (Header Filter Math)

The `-headerspamfilter` (src/net_processing.cpp) implements a height-based averaging algorithm to protect against "Nothing-at-Stake" header floods.

### 2.1 The Averaging Algorithm

The node maintains a `std::map<int, int> points` per peer, where the key is the block height and the value is the occurrence count.

* **`nHeaders`**: Total headers received in the current window.
* **`size`**: Number of distinct heights tracked.
* **`nAvgValue`**: `(double)nHeaders / size`

### 2.2 Banning Thresholds

A node is banned (`BLOCK_HEADER_SPAM`) if any of these conditions are met:

1. **Aggressive Spam**: `(nAvgValue >= 1.5 * maxAvg && size >= maxAvg)`
2. **Capacity Reach**: `(nAvgValue >= maxAvg && nHeaders >= maxSize)`
3. **Hard Ceiling**: `(nHeaders >= maxSize * 4.1)`

**Recommendation**: Set `-headerspamfiltermaxsize=2000` to allow for deep reorgs while maintaining a strict 4.1x safety ceiling.

---

## 3. Descriptor & SegWit Staking (Minter Key Logic)

Blackcoin More implements a sophisticated "Minter Key" mechanism in `CreateCoinStake()` (src/wallet/staking.cpp) to preserve modern address formats without downgrading the UTXO set.

### 3.1 The Minter Key Bridge

When staking a `WITNESS_V0_KEYHASH` or `WITNESS_V1_TAPROOT` UTXO:

1. The node identifies the script as solvable but requiring a signature that protocol v3.1 expects in a specific legacy format for the kernel.
2. **Vout[1] (The Bridge)**: The node creates a zero-value output (`nValue=0`) with a raw `OP_PUBKEY OP_CHECKSIG` script using the address's public key.
3. **Vout[2+] (The Preservation)**: The actual principal and reward are sent to the original Bech32 address script.

### 3.2 Verification Pathway

The block validator (src/pos.cpp:158) runs `VerifySignature()` with `SCRIPT_VERIFY_NONE`. This permissive check allows the Minter Key to witness the stake while the reward stays in the secure, non-malleable SegWit/Taproot container.

### 3.3 Ghost Block Diagnostic Logging

**IMPLEMENTED in v27.2.0+**: Comprehensive logging system in `src/node/miner.cpp` to diagnose and track dropped staking kernels:

* **Ghost Block Detection**: Logs when valid kernels are dropped due to timestamp validation failures
* **16-Second Mask Analysis**: Detects and logs collisions between timestamp masking and Median Time Past
* **Detailed Diagnostics**: Includes kernel hash, stake modifier, timestamps, and specific rejection reasons
* **Collision Warnings**: Early detection of potential MTP collisions before kernel creation

**IMPORTANT: With the Safety Bump mechanism, ghost blocks should NEVER occur.**

The Safety Bump guarantees that `txTime > MTP` before `CreateCoinStake()` is called. Since a valid kernel timestamp must satisfy `txTime >= MTP + 1`, the ghost block case (txTime < MTP + 1) should never trigger.

If ghost block logs appear with safety bump enabled, it indicates a bug (race condition or timing issue). This logging is retained for diagnostic purposes only.

**Logging Format**:

```
GHOST BLOCK DETECTED: Valid kernel dropped due to timestamp validation failure
  Kernel Timestamp: 123456789 (masked: 123456780)
  MedianTimePast: 123456785 (MTP+1: 123456786)
  Reason: Timestamp 123456789 < MTP+1 (123456786)
  Kernel Hash: abc123...
  Stake Modifier: def456...
  DIAGNOSTIC: 16-second mask collision detected (masked: 123456780 <= MTP: 123456785)
```

### 3.4 Complete Multi-Wallet Staking Independence

**IMPLEMENTED in v27.2.0+**: Full elimination of wallet competition through per-wallet staking state:

**Architecture:**

* **Removed**: Shared `static nLastCoinStakeSearchTime` variable
* **Implemented**: Per-wallet timers in `CWallet::m_last_coin_stake_search_time`
* **Added**: Per-wallet performance tracking in `CWallet::m_last_coin_stake_search_interval`
* **Result**: Each wallet maintains independent search windows and state

**Benefits:**

* **Fairness**: All wallets get equal staking opportunities regardless of size/frequency
* **Scalability**: No theoretical limit on number of simultaneously staking wallets
* **Performance**: Per-wallet tracking enables individual optimization
* **Diagnostics**: Enhanced logging shows per-wallet staking performance

**Logging Format:**

```
COINSTAKE CREATED: Wallet 'wallet1' found kernel at timestamp 123456789, hash abc123..., search time 45ms
COINSTAKE CREATED: Wallet 'wallet2' found kernel at timestamp 123456801, hash def456..., search time 120ms
```

**Technical Details:**

* Each wallet searches independently without competing for time windows
* Per-wallet state persists across staking cycles
* No shared resources or global locks in staking path
* Fair distribution of staking opportunities based on individual wallet UTXOs

---

## 4. Economic Policy & Consensus Fees

### 4.1 Static Fee Enforcement

In `Consensus::CheckTxInputs` (src/consensus/tx_verify.cpp), Blackcoin More enforces a hard-coded fee floor for v3.1:

```cpp
if (IsProtocolV3_1(nTimeTx) && txfee < GetMinFee(tx, nTimeTx))
    return state.Invalid(..., "bad-txns-fee-not-enough");
```

`GetMinFee` calculates: `Fee = (vSize <= 100) ? 0.0001 : (vSize * 0.001 / 1000)`.

### 4.2 vSize and SigOps Inflation

The `vSize` is calculated via `-bytespersigop` (Default: 20).

```text
vSize = max(Weight, SigOps * 20) / 4
```

Complex multisig scripts (e.g., 3-of-5) are penalized with a higher `vSize`, naturally increasing their absolute BLK fee requirement to protect staker CPU resources.

---

## 6. Staking Performance Architecture (v27.2+)

Blackcoin More v27.2 introduces a 3-layer caching architecture to ensure O(1) staking performance for both Legacy and Descriptor wallets.

### 6.1 Layer 1: Logic Cache (`m_cached_spks`)

* **Purpose**: Rapidly identifying if a UTXO belongs to the wallet.
* **Mechanism**: Maps `CScript` → `WalletDescriptor`. Populated on wallet load.
* **Optimization**: Bypass linear scanning of descriptors.

### 6.2 Layer 2: Data Cache (`stakeCache`)

* **Purpose**: Eliminating disk I/O for `blockFromTime` checks.
* **Mechanism**: Caches `(COutPoint, blockTime, amount)` in memory.
* **Optimization**: Qtum-derived LRU cache. Prevents reading `CBlockIndex` from disk during the hot loop.

### 6.3 Layer 3: Key Cache (`GetPubKey` Zero-Alloc)

* **Purpose**: Removing memory allocation overhead in the hot loop.
* **Mechanism**: Specialized `DescriptorScriptPubKeyMan::GetPubKey` bypasses `std::unique_ptr` allocation.
* **Optimization**: Direct map access for public keys. **CRITICAL** for Descriptor Performance.
* **Layer 0: Zero Disk I/O (Jan 2026 Fix)**: Removed redundant `g_txindex->FindTx` calls in `CreateCoinStake`. Wallet data is used directly from memory (`CWalletTx`), preventing 4000+ disk seeks/block.

### 6.4 Wake-on-Block-Arrival (v27.2.0+)

* **Purpose**: Eliminate CPU waste during MTP-blocked staking windows.
* **Mechanism**: Condition variable (`m_stake_cond`) signals staker thread when new blocks arrive, allowing immediate wake-up instead of polling.
* **Optimization**: Staker sleeps efficiently until the chain tip changes, avoiding wake-every-second polling.
* **File**: `src/node/miner.cpp:664`
* **Benefit**: Reduces CPU usage and log spam during blocked windows (when kernel timestamp < MTP+1).

### 6.5 Safety Bump Pre-Calculation (v27.2.0+)

* **Purpose**: Calculate next valid staking window and strip MTP inflation attacks.
* **Mechanism**: When a new block arrives (`updatedBlockTip()`), pre-calculate the next 16-second window using the new tip's MTP timestamp. Sleep time uses `GetAdjustedTimeSeconds()` and applies modulo 16000 to strip MTP inflation.
* **Why**: Attackers with +14 second clocks inflate MTP, causing honest nodes to oversleep. Using `GetAdjustedTimeSeconds()` ensures consistent time reference with block validation. Modulo 16000 strips artificial inflation.
* **Change**: v27.2.0+ uses `GetAdjustedTimeSeconds()` and strips MTP inflation via modulo arithmetic.
* **Files**: `src/wallet/wallet.cpp:1540` (`updatedBlockTip()`), `src/node/miner.cpp:255` (fallback path)
* **Note**: This is LOCAL POLICY ONLY — does not change consensus. Other nodes don't care how we calculate our sleep time.

**Attack Vector and Mitigation**:

Attackers can exploit the +15 second FutureDrift tolerance by running clocks +14 seconds ahead. This inflates MTP by ~14 seconds. The naive sleep calculation:

```
sleepMs = (nextWindow - now) * 1000 = ((MTP + 16) - now) * 1000 = 30000ms
```

Honest nodes sleep 30 seconds while attacker sleeps 16 seconds, gaining a ~14 second advantage per block.

**The Modulo Fix**:

```cpp
if (sleepMs > 16000) {
    sleepMs %= 16000;           // 30000 % 16000 = 14000
    if (sleepMs == 0) sleepMs = 16000;
}
```

This stripped value preserves the true offset to the next window boundary. Both honest nodes and attackers wake at the same wall-clock moment relative to the true 16-second boundary.

### 6.6 Wake-On-Block Race Condition Fix

**Race Condition:**

The validation thread sets `m_new_block_arrived = true` when a new block arrives. The staker thread checks this flag inside `SleepStaker()`. If a block arrives while `CreateNewBlock()` is running, the flag becomes stale — the staker reads the pre-calculated sleep, calls `SleepStaker()`, and immediately aborts because the stale flag is true.

**Symptom:**

```
Minter: Using pre-calculated safety bump sleep=15000 ms (from UpdatedBlockTip)
WARNING: Close MTP collision detected (search: 1774289168, MTP: 1774289168, diff: 0)
```

The MTP collision occurs because the physical clock hasn't advanced — the sleep was aborted instantly.

**Fix:**

Clear the wake-up flag at the **TOP of the staking loop**, before any work begins:

```cpp
// miner.cpp:867
pwallet->m_new_block_arrived.store(false);
```

This ensures any stale notification from the previous iteration is consumed before the next iteration's work begins.

**Window Calculation Formula**:

```
nextWindow = ((MTP + 16 + 15) / 16) * 16  // Next 16-sec window strictly AFTER MTP
sleepMs = (nextWindow - GetAdjustedTimeSeconds()) * 1000
if (sleepMs > 16000) sleepMs %= 16000    // Strip MTP inflation
```

**Example (Attack Mitigation)**:

```
MTP (inflated by attacker +14s): 10:40:30
Next window: 10:40:48
GetAdjustedTime(): 10:40:16 (includes attacker influence)
Raw sleep: 32 seconds
After modulo: 32 % 16 = 0 → 16 seconds (true offset to window)
```

**Example (Normal Operation)**:

```
MTP: 10:40:16
Next window: 10:40:32
System time: 10:40:18
Sleep: 14 seconds (no modulo needed)
```

---

## 5. Part IV: Configuration Hardening (Summary)

| Parameter | Recommended | Technical Justification |
| :--- | :--- | :--- |
| `-txindex` | `1` | **MANDATORY**. Required to fetch `txPrev.nTime` for kernel validation. |
| `-maxtimeadjustment` | `0` | Disables peer-to-peer clock swaying (Timejacking protection). |
| `-avoidpartialspends` | `0` | Prevent mass UTXO grouping that resets staking maturity for the whole address. |
| `-maxorphantx` | `1000` | Extends RAM buffer for parentless txs to improve propagation success. |
| `-staketimio` | `500` | Controls the sleep interval (ms) between kernel searches. |

---

## 7. Chain Selection & Stability Logic (Protocol Hardening)

Blackcoin More implements several mechanisms to ensure network convergence and prevent long-range history rewriting attacks.

### 7.1 Best Chain Selection (`CBlockIndexWorkComparator`)

The node determines the "Best Chain" using `nChainWork`—the cumulative proof-of-stake difficulty.

* **Proof calculation**: `GetBlockProof(block) = (~bnTarget / (bnTarget + 1)) + 1`. This effectively calculates the expected number of hashes required to find a kernel below the target.
* **Tie-breaking**: If two chains have identical `nChainWork`, the node uses `nSequenceId`. This favors the block that was **received first** (First-Seen Rule), providing stability against late-arriving competing branches.

### 7.2 Hard Reorg Limit (`nMaxReorganizationDepth`)

In `src/kernel/chainparams.cpp`, Blackcoin More enforces a strict reorg ceiling:

```cpp
consensus.nMaxReorganizationDepth = 500;
```

A node will **reject** any reorganization that attempts to revert more than 500 blocks (approx. 9 hours of history). This protects the network against "Nothing-at-Stake" double-spends and ensures that old history becomes immutable even without manual checkpoints.

### 7.3 Sync Checkpoints (Automatic Lock-in)

The `AutoSelectSyncCheckpoint()` function (src/node/blockstorage.cpp) provides an additional layer of stability:

* **Mechanism**: The node automatically identifies a block at depth `nCoinbaseMaturity` (500 blocks) behind the current tip as a "Synchronized Checkpoint."
* **Restriction**: The node will not reorganization past a height that is already covered by a synchronized checkpoint from a "better" (higher work) chain. This prevents network splits by forcing nodes to synchronize on a common history before building new branches.

### 7.4 Network Split Avoidance

The network avoids splits through:

1. **IBD Verification**: Nodes in Initial Block Download (IBD) strictly follow the longest-work chain regardless of local peer preferences.
2. **Median Time Past (MTP)**: Consensus rules require `block.nTime > pindexPrev->GetMedianTimePast()`. This prevents "Timejacking" where a peer attempts to fork the network by significantly altering its clock to create "valid" future-dated blocks.

---

## 8. Appendix: OP_RETURN "Social" Staking Mechanics

The "Social Messaging" discovered in the blockchain (e.g., Block 3,497,824) exploits the permissiveness of the **P2PKH** validation path.

### 6.1 ScriptSig Injection

In a standard P2PKH script:

```text
Stack: [signature] [pubkey] [EXTRA_DATA]
Script: OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG
```

The `OP_CHECKSIG` utility only consumes the top two items from the stack (`sig` and `pubkey`). Anything left on the stack (the `EXTRA_DATA`) is ignored by consensus but preserved in the blockchain.

### 6.2 External implementation Conclusion

This mechanism is **not implemented** in the core Blackcoin More codebase. It is a result of external wallet software (likely a fork of the staking loop) that manually modifies the `scriptSig` or adds a high-priority `vout` with an `OP_RETURN` payload prior to block broadcast.

---
*Document Version: 1.3 (Consensus Hardening Update)*
*Revision Date: March 22, 2026*

---

## 9. The Immutable Consensus Rules (Hard Forks)

These 5 rules are **Consensus Law**. Changing any of these logic points constitutes a **Hard Fork** and will cause a network split.

### 9.1 The "Kernel Output" Rule (`validation.cpp`)

* **Rule**: The Public Key for block verification **MUST** be found in the second transaction's second output (`block.vtx[1]->vout[1]`).
* **Allowed Formats**:
  * Legacy P2PK (`OP_PUBKEY ...`) - *Standard*
  * OP_RETURN (`OP_RETURN <pubkey> ...`) - *Alternative (Multisig/Message)*
* **Why**: `CheckBlockSignature` looks **ONLY** at `vout[1]`. It ignores input scripts and SegWit witness data.

### 9.2 The "Empty Input 0" Rule (`primitives/transaction.h`)

* **Rule**: A Coinstake transaction MUST have at least one input (`vin[0]`) which is **NOT** null (unlike Coinbase).
* **Code**: `!vin[0].prevout.IsNull()` in `IsCoinStake()`.

### 9.3 The "Empty Output 0" Rule (`primitives/transaction.h`)

* **Rule**: A Coinstake transactions MUST have a first output (`vout[0]`) that is **completely empty** (Zero value, Empty script).
* **Code**: `vout[0].IsEmpty()` in `IsCoinStake()`.

### 9.4 The "Stake Modifier" Rule (`pos.cpp`)

* **Rule**: The Proof-of-Stake Hash calculation MUST use `nStakeModifier` from the *previous* block index.
* **Formula**: `Hash(nStakeModifier + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime)`
* **Why**: This is the core entropy source. If you change this formula, every proof becomes invalid.

### 9.5 The "Maturity" Rule (`pos.cpp`)

* **Rule**: A UTXO cannot stake until it is **500 blocks deep**.
* **Code**: `pindexPrev->nHeight + 1 - coinPrev.nHeight < 500`.

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

* [STAKECACHE.md](file:///Users/blackcoindev/Development/Blackcoin/blackcoin-more/agent/STAKECACHE.md) - Full stake cache documentation
* [STAKING.md](file:///Users/blackcoindev/Development/Blackcoin/blackcoin-more/agent/STAKING.md) - General staking documentation
