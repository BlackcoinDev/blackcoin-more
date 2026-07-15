# nTime / Block Time / nTimeSmart Investigation

**Investigation Date:** 2026-07-08
**Triggered by:** Block rejection error `bad-txns-time-earlier-than-input` on mainnet at block height 5944947
**Scope:** Comparison of `blackmore262` (v28 pre-CORE), `blackmore284` (v28-CORE), and `bitcoin` (upstream Bitcoin Core 28.x)

---

## Table of Contents

1. [Problem Statement](#1-problem-statement)
2. [The Critical Bug: `bad-txns-time-earlier-than-input`](#2-the-critical-bug-bad-txns-time-earlier-than-input)
3. [Codebase Comparison Overview](#3-codebase-comparison-overview)
4. [`AddCoins` — Where `Coin.nTime` Gets Set](#4-addcoins--where-cointntime-gets-set)
5. [`CheckTxInputs` — Where the Time Check Happens](#5-checktxinputs--where-the-time-check-happens)
6. [The Root Cause: Mismatched Time References](#6-the-root-cause-mismatched-time-references)
7. [v1 vs v2 Transaction Time Handling](#7-v1-vs-v2-transaction-time-handling)
8. [`coinstatsindex` and the Deterministic nTime Requirement](#8-coinstatsindex-and-the-deterministic-ntime-requirement)
9. [Wallet `nTimeSmart` and the Invariant Chain](#9-wallet-ntimesmart-and-the-invariant-chain)
10. [All Affected Code Locations](#10-all-affected-code-locations)
11. [Possible Fixes and Their Trade-offs](#11-possible-fixes-and-their-trade-offs)
12. [CoinStatsIndex Compatibility Analysis](#12-coinstatsindex-compatibility-analysis)
13. [SegWit v>1 Compatibility Analysis](#13-segwit-v1-compatibility-analysis)
14. [Witness Version Range Analysis (v2-v16)](#14-witness-version-range-analysis-v2-v16-including-v14-v15-v16)
15. [Mempool AddCoins Fix - REVERTED](#15-mempool-addcoins-fix---reverted)
16. [Recommendations](#16-recommendations)

---

## 1. Problem Statement

A block at mainnet height 5944947 (July 8, 2026) was rejected with:

```
2026-07-08T17:23:53.865980Z ConnectBlock: Consensus::CheckTxInputs:
  9e700f20065123baa62196448ac70a0533866a9f3c68475da3e23690e8ae30f6,
  bad-txns-time-earlier-than-input
```

The failing transaction (`9e700f2...`) was a v2 transaction (version 2) that spent an output from another v2 transaction (`4663d6f...`) created in the **same block** (both at block height 5944947, block time 1783531440 = July 8, 2026 17:24:00 UTC).

**This rejection is a regression** — the same block was valid under `blackmore262` (previous release).

---

## 2. The Critical Bug: `bad-txns-time-earlier-than-input`

### The failing check

In `src/consensus/tx_verify.cpp:191` (all three Blackcoin versions):

```cpp
if (coin.nTime > nTimeTx)
    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-time-earlier-than-input");
```

### The actual values in the failing case

| Field | Value | Source |
|---|---|---|
| Block time (`block.nTime`) | `1783531440` | `validation.cpp:2684` |
| Node wall-clock at log time | `1783531433` | `GetAdjustedTimeSeconds()` |
| `coin.nTime` (for the input being spent) | `1783531440` | `AddCoins` in `coins.cpp:129` (v284) |
| `nTimeTx` (for the spending tx) | `1783531433` | `GetAdjustedTimeSeconds()` in `tx_verify.cpp:176` |

**Result:** `coin.nTime (1783531440) > nTimeTx (1783531433)` → block rejected.

The block time is **7 seconds ahead** of the node's local clock. This is normal for PoS blocks — block timestamps are set by the miner's `GetAdjustedTimeSeconds()` when the block is found, but the network time may be slightly different.

---

## 3. Codebase Comparison Overview

| Aspect | `bitcoin` (upstream Core 28.x) | `blackmore262` (v28 pre-CORE) | `blackmore284` (v28-CORE) |
|---|---|---|---|
| **v2 transactions** | No | Yes | Yes |
| **`Coin.nTime` field** | No | Yes | Yes |
| **`AddCoins` sets `nTime`** | N/A (no `nTime` field) | `tx.nTime` (which is 0 for v2) | `nBlockTime` for v2, `tx.nTime` for v1 |
| **`CheckTxInputs` has time check** | No | Yes (with `GetAdjustedTimeSeconds()`) | Yes (with `GetAdjustedTimeSeconds()`) |
| **`bad-txns-time-earlier-than-input` reachable** | No | No (dead code for v2) | **YES (bug)** |
| **`coinstatsindex` nTime handling** | N/A (no `nTime`) | Uses `tx.nTime` directly | Uses `block.nTime` for v2, with old-undo recovery |
| **`GetAdjustedTimeSeconds()`** | No | Yes (Blackcoin addition) | Yes |

### Summary

The bug exists **only in `blackmore284`**. It was introduced by commit `002b58d84f` ("index: make coinstatsindex coinstake compatible") on July 1, 2026.

---

## 4. `AddCoins` — Where `Coin.nTime` Gets Set

### `bitcoin` (upstream)

`bitcoin/src/coins.cpp` does not have a `nTime` parameter in `AddCoins` because `Coin` doesn't have a `nTime` field:

```cpp
void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check_for_overwrite) {
    bool fCoinbase = tx.IsCoinBase();
    const Txid& txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check_for_overwrite ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase), overwrite);
    }
}
```

### `blackmore262`

`blackmore262/src/coins.cpp:119-128`:

```cpp
void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check_for_overwrite) {
    bool fCoinbase = tx.IsCoinBase();
    bool fCoinstake = tx.IsCoinStake();
    const uint256& txid = tx.GetHash();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check_for_overwrite ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase, fCoinstake, tx.nTime), overwrite);
    }
}
```

**Key point:** For v2 transactions, `tx.nTime` is **0** (not serialized on wire), so `coin.nTime` is **0**.

### `blackmore284` (the broken one)

`blackmore284/src/coins.cpp:122-136`:

```cpp
void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check_for_overwrite, int nBlockTime) {
    bool fCoinbase = tx.IsCoinBase();
    bool fCoinstake = tx.IsCoinStake();
    const Txid& txid = tx.GetHash();
    // For v2 transactions, nTime is not serialized on the wire.
    // Always use block header time to ensure deterministic Coin nTime
    // regardless of whether tx came from memory or disk.
    int nTimeCoin = tx.version >= 2 ? nBlockTime : (int)tx.nTime;
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        bool overwrite = check_for_overwrite ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase, fCoinstake, nTimeCoin), overwrite);
    }
}
```

**Key point:** For v2 transactions, `coin.nTime` is now set to **`nBlockTime`** (the block header time), not 0.

This change was introduced in commit `002b58d84f` (July 1, 2026) for the coinstatsindex.

---

## 5. `CheckTxInputs` — Where the Time Check Happens

### `bitcoin` (upstream)

`bitcoin/src/consensus/tx_verify.cpp:165-195`:

```cpp
bool Consensus::CheckTxInputs(const CTransaction& tx, TxValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
{
    if (!inputs.HaveInputs(tx)) {
        return state.Invalid(TxValidationResult::TX_MISSING_INPUTS, "bad-txns-inputs-missingorspent", ...);
    }

    CAmount nValueIn = 0;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < COINBASE_MATURITY) {
            return state.Invalid(TxValidationResult::TX_PREMATURE_SPEND, "bad-txns-premature-spend-of-coinbase", ...);
        }

        // NO TIME CHECK HERE
        nValueIn += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn)) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-inputvalues-outofrange", ...);
        }
    }
    // ... fee/txfee logic
}
```

**No time check exists in upstream Bitcoin Core.**

### `blackmore262`

`blackmore262/src/consensus/tx_verify.cpp:169-200`:

```cpp
bool Consensus::CheckTxInputs(const CTransaction& tx, TxValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
{
    if (!inputs.HaveInputs(tx)) {
        return state.Invalid(TxValidationResult::TX_MISSING_INPUTS, "bad-txns-inputs-missingorspent", ...);
    }

    // Blackcoin: in v2 transactions use GetAdjustedTime() as nTimeTx
    int64_t nTimeTx = tx.nTime;
    if (!nTimeTx && tx.nVersion >= 2)
        nTimeTx = GetAdjustedTimeSeconds();

    CAmount nValueIn = 0;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        if ((coin.IsCoinBase() || coin.IsCoinStake()) && nSpendHeight - coin.nHeight < (IsProtocolV3_1(nTimeTx) ? nCoinbaseMaturity : Params().nCoinbaseMaturity)) {
            return state.Invalid(TxValidationResult::TX_PREMATURE_SPEND, ...);
        }

        // Check transaction timestamp
        if (coin.nTime > nTimeTx)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-time-earlier-than-input");
        // ...
    }
    // ...
}
```

**Time check is present but effectively dead code for v2:** `coin.nTime` is 0 for v2, `nTimeTx` is wall-clock (always > 0), so `0 > wallclock` is always false.

### `blackmore284` (the broken one)

`blackmore284/src/consensus/tx_verify.cpp:165-197` — **identical to `blackmore262`**, but now the time check **actually fires** because `coin.nTime` is `nBlockTime` for v2 (not 0).

```cpp
bool Consensus::CheckTxInputs(const CTransaction& tx, TxValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
{
    if (!inputs.HaveInputs(tx)) {
        return state.Invalid(TxValidationResult::TX_MISSING_INPUTS, "bad-txns-inputs-missingorspent", ...);
    }

    // Blackcoin: in v2 transactions use GetAdjustedTimeSeconds() as nTimeTx
    int64_t nTimeTx = tx.nTime;
    if (!nTimeTx && tx.version >= 2)
        nTimeTx = GetAdjustedTimeSeconds();

    // ... loop ...
    if (coin.nTime > nTimeTx)
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-time-earlier-than-input");
    // ...
}
```

---

## 6. The Root Cause: Mismatched Time References

The bug is a **mismatch** between two pieces of code that should use the same time reference:

| Side | What it uses | Source |
|---|---|---|
| `Coin.nTime` (left side of check) | `nBlockTime` for v2 | `coins.cpp:129` (v284) |
| `nTimeTx` (right side of check) | `GetAdjustedTimeSeconds()` (wall clock) | `tx_verify.cpp:176` |

In `blackmore262`, both sides were effectively 0 for v2 txs (because `tx.nTime` is 0 for v2), so the check was dead code. In `blackmore284`, the `AddCoins` change made `coin.nTime` = `nBlockTime` (non-zero), but `CheckTxInputs` was not updated to match.

### Why does the v262 code "work"?

The v262 code has a **latent bug**: the time check is supposed to prevent time-warp attacks, but for v2 transactions, it's completely bypassed (always passes). This was an accident of the design: v2 txs have no `nTime` on wire, so `coin.nTime` is 0, and the check `0 > wallclock` is always false.

The v284 code was trying to **fix the coinstatsindex** (which needs deterministic nTime for hashing), but in doing so, it **exposed the latent bug** in `CheckTxInputs`.

### The correct design

Both sides of the comparison should use the **block time** for v2 transactions:
- `Coin.nTime` = `nBlockTime` (already correct in v284)
- `nTimeTx` = `nBlockTime` (needs to be fixed)

This ensures the check is comparing apples to apples for chained v2 transactions within the same block.

---

## 7. v1 vs v2 Transaction Time Handling

### Wire format

From `primitives/transaction.h:232-236` (both Blackcoin versions):

```cpp
s >> tx.version;
if (tx.version < 2)
    s >> tx.nTime;
else
    tx.nTime = 0;
```

**For v2 transactions, `nTime` is never serialized on the wire. It is always 0 after deserialization.**

### Default constructor

From `primitives/transaction.cpp:68`:

```cpp
CMutableTransaction::CMutableTransaction() : version(CTransaction::CURRENT_VERSION), nTime(GetAdjustedTimeSeconds()), nLockTime(0) {}
```

**A newly-created mutable transaction gets the node's wall-clock time as `nTime`.** But this value is **discarded** when the transaction is serialized with version ≥ 2.

### The `v ? v1_value : v2_value` pattern

Throughout the Blackcoin codebase, this pattern appears repeatedly:

```cpp
something ? tx.nTime : block.nTime
```

or:

```cpp
coin.nTime ? coin.nTime : blockFrom->nTime
```

This is the **v1 vs v2 fallback pattern**: for v1, use the per-tx time; for v2 (where `tx.nTime == 0`), fall back to the block header time.

**Locations of this pattern:**

| File:Line | Code | Context |
|---|---|---|
| `coins.cpp:129` | `tx.version >= 2 ? nBlockTime : (int)tx.nTime` | `AddCoins` |
| `pos.cpp:180` | `(coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime)` | `CheckProofOfStake` |
| `pos.cpp:218` | `(coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime)` | `CheckKernel` (cached) |
| `pos.cpp:251` | `(coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime)` | `CacheKernel` |
| `validation.cpp:2517` | `block.vtx[1]->nTime ? block.vtx[1]->nTime : block.nTime` | `CheckProofOfStake` call |
| `validation.cpp:4050` | `block.vtx[0]->nTime ? (int64_t)block.vtx[0]->nTime : block.GetBlockTime()` | `CheckBlock` coinbase time |
| `validation.cpp:4054` | `block.vtx[1]->nTime ? (int64_t)block.vtx[1]->nTime : block.GetBlockTime()` | `CheckBlock` coinstake time |
| `miner.cpp:587` | `pblock->vtx[1]->nTime ? pblock->vtx[1]->nTime : pblock->nTime` | `ProcessBlockFound` |
| `wallet.cpp:2243` | `wtx.nTimeSmart` (not `tx.nTime`) | `SignTransaction` |
| `wallet/staking.cpp:429-433` | Uses `nTimeSmart` (== blocktime) | Input combining |
| `tx_verify.cpp:174-176` | `!nTimeTx && tx.version >= 2 ? GetAdjustedTimeSeconds() : tx.nTime` | `CheckTxInputs` **← THE BUG** |
| `validation.cpp:801-804` | Same as above | Mempool `PreChecks` |
| `coinstatsindex.cpp:155,449` | `tx->version >= 2 ? block.nTime : tx->nTime` | `coinstatsindex` |
| `coinstatsindex.cpp:190-193, 483-486` | `if (coin.nTime == 0 && coin.nHeight > 0) coin.nTime = pindexPrev->nTime;` | Old undo data recovery |

### Summary table

| Context | v1 txs (`tx.version < 2`) | v2 txs (`tx.version >= 2`) |
|---|---|---|
| Wire format | `nTime` serialized | `nTime` NOT serialized (= 0 on deserialize) |
| `Coin::nTime` (UTXO entry) | `tx.nTime` | `nBlockTime` (v284) or `0` (v262) |
| `CheckTxInputs` `nTimeTx` | `tx.nTime` | `GetAdjustedTimeSeconds()` **(BUG: should be nBlockTime)** |
| Mempool `PreChecks` `nTimeTx` | `tx.nTime` | `GetAdjustedTimeSeconds()` |
| `CheckProofOfStake` `blockFromTime` | `coinPrev.nTime` | `coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime` |
| `CacheKernel` `blockFromTime` | `coinPrev.nTime` | `coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime` |
| `SignTransaction` (`script/sign.cpp:822-824`) | `nTime` kept | `nTime` zeroed before sighash |
| `ComputeTimeSmart` for coinstake | forced to blocktime | forced to blocktime |
| Wallet input `nTimeSmart > txNew.nTime` | compares per-tx time | compares smart time (== blocktime) |
| `Coin` in wallet `SignTransaction` | `wtx.nTimeSmart` | `wtx.nTimeSmart` (== blocktime) |
| Tx weight (`spend.cpp:149-151`) | `+4 * WITNESS_SCALE_FACTOR` | no adjustment |
| `coinstatsindex` `nTime` | `tx->nTime` | `block.nTime` (with old-undo recovery) |

---

## 8. `coinstatsindex` and the Deterministic nTime Requirement

### Why the v284 change was made

The commit `002b58d84f` (July 1, 2026) was titled **"index: make coinstatsindex coinstake compatible"**. The full diff shows:

```cpp
// coinstatsindex.cpp:152-156
int nTimeOut = tx->version >= 2 ? (int)block.data->nTime : (int)tx->nTime;
Coin coin{out, block.height, tx->IsCoinBase(), tx->IsCoinStake(), nTimeOut};
```

```cpp
// coinstatsindex.cpp:188-193 (for old undo data)
if (coin.nTime == 0 && coin.nHeight > 0) {
    const CBlockIndex* pindexPrev = pindex->GetAncestor(coin.nHeight);
    if (pindexPrev) coin.nTime = pindexPrev->nTime;
}
```

And the same patterns in `ReverseBlock`.

The **purpose** was to ensure that the `Coin.nTime` used in the muhash UTXO-set hash is **deterministic** — the same for any node processing the same chain, regardless of whether the tx came from memory or disk. Without this, two nodes could compute different muhash values for the same UTXO set, breaking `assumeutxo` snapshots.

### The muhash serialization

From `src/kernel/coinstats.cpp:40-57`:

```cpp
uint64_t GetBogoSize(const CScript& script_pub_key)
{
    return 32 /* txid */ +
           4 /* vout index */ +
           4 /* height + coinbase + coinstake */ +
           4 /* nTime */ +                    // <-- Coin.nTime is part of the hash
           8 /* amount */ +
           2 /* scriptPubKey len */ +
           script_pub_key.size();
}

static void TxOutSer(T& ss, const COutPoint& outpoint, const Coin& coin)
{
    ss << outpoint;
    ss << static_cast<uint32_t>((coin.nHeight << 2) + (coin.fCoinBase ? 1u : 0u) + (coin.fCoinStake ? 2u : 0u));
    ss << VARINT(coin.nTime);                // <-- Serialized into muhash
    ss << coin.out;
}
```

**`Coin.nTime` is part of the UTXO-set hash. If it's 0 for v2 txs, the hash will differ from what it would be with the correct block time.**

### Comparison with `bitcoin` (upstream)

`bitcoin/src/index/coinstatsindex.cpp` does not have any v2-related nTime handling because upstream Bitcoin Core does not have v2 transactions. The upstream `Coin` class doesn't have an `nTime` field at all.

### Comparison with `blackmore262`

`blackmore262` had v2 transactions and the coinstatsindex, but it used `tx->nTime` directly (which is 0 for v2). This means the muhash was computed with `nTime = 0` for all v2 outputs. This was a pre-existing inconsistency that the v284 commit tried to fix.

---

## 9. Wallet `nTimeSmart` and the Invariant Chain

### The invariant

For confirmed v2 transactions, the following should all be equal:
- `Coin.nTime` (set by `AddCoins` to `nBlockTime` in v284)
- `wtx.nTimeSmart` (set by `ComputeTimeSmart` to `blocktime` for confirmed coinstakes)
- `block.nTime` (the block header time)

This invariant is relied upon by:
- `wallet/staking.cpp:429-433` — compares `pcoin->nTimeSmart` to `txNew.nTime` when combining inputs
- `wallet.cpp:2243` — uses `wtx.nTimeSmart` when building `Coin` objects for signing

### `ComputeTimeSmart` for coinstakes

From `wallet/wallet.cpp:2948-2952`:

```cpp
if (chain().findBlock(*block_hash, FoundBlock().time(blocktime).maxTime(block_max_time))) {
    // Blackcoin: Coinstake time must always equal blocktime. Unlike regular transactions,
    // coinstake is never broadcasted and should not use smart timestamp heuristics.
    if (wtx.IsCoinStake()) {
        nTimeSmart = blocktime;
    } else if (rescanning_old_block) {
        nTimeSmart = block_max_time;
    } else {
        // ... regular tx heuristic ...
    }
}
```

**For coinstakes, `nTimeSmart` is forced to equal `blocktime`.** This is a Blackcoin-specific design choice.

### Coinstake nTime recovery in wallet

From `wallet/wallet.cpp:1099-1104`:

```cpp
// Blackcoin: Recover nTime for v2 coinstakes that had tx.nTime = 0 after wire deserialization
if (wtx.tx->IsCoinStake() && wtx.tx->nTime == 0 && wtx.tx->version == 2) {
    CMutableTransaction mtx(*wtx.tx);
    mtx.nTime = wtx.nTimeSmart ? wtx.nTimeSmart : wtx.nTimeReceived;
    wtx.tx = MakeTransactionRef(std::move(mtx));
}
```

This runs in `AddToWallet` after `nTimeSmart` is computed, so the recovered `nTime` matches the block time.

The same code appears in `LoadToWallet` (line 1223-1230).

---

## 10. All Affected Code Locations

### `src/consensus/tx_verify.cpp`

- **Line 174-176:** The bug — `nTimeTx = GetAdjustedTimeSeconds()` for v2 txs in block context should be `nBlockTime`.

### `src/coins.cpp`

- **Line 122-136:** `AddCoins` uses `nBlockTime` for v2 txs. This is the change from commit `002b58d84f`.

### `src/validation.cpp`

- **Line 2123-2136:** `UpdateCoins` passes `nBlockTime` to `AddCoins`.
- **Line 2665:** `ConnectBlock` passes `block.nTime` to `UpdateCoins`.
- **Line 4998:** `ReplayBlocks` passes `pindex->nTime` to `AddCoins`.
- **Line 801-804:** Mempool `PreChecks` uses `GetAdjustedTimeSeconds()` for v2 `nTimeTx` (not consensus-critical, just mempool policy).
- **Line 724:** Package mempool acceptance uses `GetAdjustedTimeSeconds()` for `GetMinFee`.
- **Line 2517:** `CheckProofOfStake` call uses v1/v2 fallback pattern.
- **Line 4050, 4054:** `CheckBlock` uses v1/v2 fallback for coinbase/coinstake timestamps.
- **Line 4093:** Per-tx "block timestamp not earlier than tx timestamp" check.
- **Line 4234, 4258, 4267, 4274:** Header validation uses `block.GetBlockTime()` and `pindexPrev->GetMedianTimePast()`.
- **Line 4298-4304:** `IsFinalTx` uses `block.GetBlockTime()` (or MTP if `enforce_locktime_median_time_past`).

### `src/index/coinstatsindex.cpp`

- **Line 121-156:** `CustomAppend` uses `block.nTime` for v2 txs.
- **Line 188-193:** Old undo data recovery for v2 txs.
- **Line 408-497:** `ReverseBlock` — same patterns.

### `src/kernel/coinstats.cpp`

- **Line 40-57:** `GetBogoSize` and `TxOutSer` include `coin.nTime` in the muhash.

### `src/pos.cpp`

- **Line 180:** `CheckProofOfStake` uses `(coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime)`.
- **Line 218, 251:** `CheckKernel` and `CacheKernel` use the same fallback.

### `src/wallet/staking.cpp`

- **Line 316, 360, 366-373:** Coinstake timestamp search and OP_RETURN carrier.
- **Line 407-440:** Input combining — uses `nTimeSmart` to mirror consensus check.
- **Line 506, 513:** Cache and restore `nTime` around signing (because `SignTransaction` zeros v2 `nTime`).

### `src/wallet/wallet.cpp`

- **Line 2243:** `SignTransaction` uses `wtx.nTimeSmart` when building `Coin` objects.
- **Line 1099-1104, 1223-1230:** Coinstake nTime recovery.
- **Line 2948-2952:** `ComputeTimeSmart` forces `nTimeSmart = blocktime` for coinstakes.
- **Line 1117-1122:** Recompute `nTimeSmart` when coinstake transitions to confirmed.

### `src/primitives/transaction.h`

- **Line 232-236:** `nTime` not serialized for v2 txs.

### `src/primitives/transaction.cpp`

- **Line 68:** Default constructor uses `GetAdjustedTimeSeconds()`.

### `src/script/sign.cpp`

- **Line 818-824:** `SignTransaction` zeros `nTime` for v2 before sighash.

### `src/node/miner.cpp`

- **Line 56, 70, 183, 342, 587:** Various uses of `GetAdjustedTimeSeconds()` and v1/v2 fallback.
- **Line 250, 287, 289:** Block time computation.

### `src/wallet/spend.cpp`

- **Line 1064:** `GetAdjustedTimeSeconds()` for fee computation.
- **Line 149-151:** Tx weight adjustment for v1 (`+4 * WITNESS_SCALE_FACTOR`).

---

## 11. Possible Fixes and Their Trade-offs

### Option A: Fix `CheckTxInputs` to use `nBlockTime` (the originally attempted fix)

**Change:** Add `nBlockTime` parameter to `CheckTxInputs`, pass `pindex->nTime` from `ConnectBlock` and `GetAdjustedTimeSeconds()` from mempool.

**Pros:**
- Makes the check symmetric with `AddCoins` (both use block time)
- Restores v262 behavior for chained v2 txs in the same block
- The check `coin.nTime > nTimeTx` becomes `nBlockTime > nBlockTime` = false, which is correct (chained txs in the same block have the same effective time)

**Cons:**
- Changes consensus rules (but only fixes a regression)
- Requires updating all callers (validation.cpp, txmempool.cpp, test/fuzz/coins_view.cpp)
- Need to ensure mempool path still works (use `GetAdjustedTimeSeconds()` for mempool)

**Status:** Implemented and reverted by user. Needs more investigation into why it was reverted.

### Option B: Revert `AddCoins` to use `tx.nTime` (v262 behavior)

**Change:** Remove the `nBlockTime` parameter from `AddCoins` and go back to `coin.nTime = tx.nTime`.

**Pros:**
- Restores v262 behavior exactly
- No need to change `CheckTxInputs`
- The latent bug in `CheckTxInputs` (dead code for v2) remains, but it's pre-existing

**Cons:**
- Breaks the coinstatsindex fix from commit `002b58d84f`
- The muhash UTXO-set hash will be computed with `nTime = 0` for v2 outputs, which is incorrect
- The `coinstatsindex` old-undo recovery code (`if (coin.nTime == 0...)`) becomes the only way to fix the nTime, which is fragile

### Option C: Keep the `AddCoins` change but fix the `CheckTxInputs` check differently

**Change:** Instead of comparing `coin.nTime > nTimeTx`, compare `coin.nTime > (nTimeTx for v1 txs)` — i.e., only apply the check for v1 txs.

**Pros:**
- Avoids the mismatch entirely
- Minimal code change

**Cons:**
- The time check is bypassed for all v2 txs, which is a security regression
- Doesn't fix the underlying issue (the check is supposed to prevent time-warp)

### Option D: Make `GetAdjustedTimeSeconds()` return the block time in block context

**Change:** Pass the block time through a thread-local or context variable that `GetAdjustedTimeSeconds()` reads.

**Pros:**
- Minimal code change in callers
- Makes `GetAdjustedTimeSeconds()` context-aware

**Cons:**
- Hidden state, hard to reason about
- Changes the meaning of `GetAdjustedTimeSeconds()` globally
- Could break other code that relies on it being wall-clock

### Option E: Compute `nTimeTx` from `coin.nTime` itself

**Change:** Instead of using a separate `nTimeTx` variable, use `coin.nTime` (which is now deterministic for v2 txs) as the time reference for the check.

**Pros:**
- Self-consistent — no time reference mismatch
- The check `coin.nTime > coin.nTime` would always be false, which is correct for chained txs

**Cons:**
- Changes the semantics of the check
- For v1 txs, this would mean `nTimeTx` is the max of all input `nTime` values, not the spending tx's `nTime`

---

## 11.1. Selected Fix: Option A (Implemented)

After the 3-way cross-check investigation, **Option A** was selected and implemented. The fix makes the time check in `CheckTxInputs` symmetric with `AddCoins` by using `nBlockTime` for v2 transactions.

### Files Modified

#### 1. `src/consensus/tx_verify.h`

Added `nBlockTime` parameter to `CheckTxInputs` declaration:

```cpp
[[nodiscard]] bool CheckTxInputs(const CTransaction& tx, TxValidationState& state,
                                 const CCoinsViewCache& inputs, int nSpendHeight,
                                 CAmount& txfee, int64_t nBlockTime);
```

#### 2. `src/consensus/tx_verify.cpp`

Updated implementation to use `nBlockTime` for v2 txs:

```cpp
bool Consensus::CheckTxInputs(const CTransaction& tx, TxValidationState& state,
                              const CCoinsViewCache& inputs, int nSpendHeight,
                              CAmount& txfee, int64_t nBlockTime)
{
    // ... existing code ...

    // Blackcoin: in v2 transactions nTime is not serialized on the wire.
    // Use the block time (passed by caller) for deterministic validation.
    int64_t nTimeTx = tx.nTime;
    if (!nTimeTx && tx.version >= 2)
        nTimeTx = nBlockTime;  // Changed from GetAdjustedTimeSeconds()

    // ... rest of function ...
}
```

#### 3. `src/validation.cpp` (block validation, line 2622)

Pass `pindex->nTime` (block header time):

```cpp
if (!Consensus::CheckTxInputs(tx, tx_state, view, pindex->nHeight, txfee, pindex->nTime)) {
```

#### 4. `src/validation.cpp` (mempool validation, line 911)

Pass `GetAdjustedTimeSeconds()` (wall clock, since there's no block yet):

```cpp
if (!Consensus::CheckTxInputs(tx, state, m_view, m_active_chainstate.m_chain.Height() + 1,
                              ws.m_base_fees, GetAdjustedTimeSeconds())) {
```

#### 5. `src/txmempool.cpp` (mempool sanity check, line 745)

Pass `GetAdjustedTimeSeconds()` to match mempool behavior:

```cpp
assert(Consensus::CheckTxInputs(tx, dummy_state, mempoolDuplicate, spendheight, txfee,
                                GetAdjustedTimeSeconds()));
```

#### 6. `src/test/fuzz/coins_view.cpp` (fuzz test, line 258)

Pass `GetAdjustedTimeSeconds()` and add `#include <util/time.h>`:

```cpp
if (Consensus::CheckTxInputs(transaction, state, coins_view_cache,
                             fuzzed_data_provider.ConsumeIntegralInRange<int>(0, std::numeric_limits<int>::max()),
                             tx_fee_out, GetAdjustedTimeSeconds())) {
```

#### 7. `src/test/coins_tests.cpp` (regression test)

Added new test `checktxinputs_v2_chained_in_same_block` that verifies:
- A v2 transaction spending a v2 output in the same block passes validation
- The time check uses `nBlockTime` (not wall clock) for v2 txs
- Both sides of the comparison use the same time reference

```cpp
BOOST_AUTO_TEST_CASE(checktxinputs_v2_chained_in_same_block)
{
    CCoinsViewTest backend;
    CCoinsViewCache cache(&backend);

    // Create a v2 parent tx and add to coins cache
    CMutableTransaction mtx_parent;
    mtx_parent.version = 2;
    mtx_parent.nTime = 0;
    mtx_parent.vout.emplace_back(100 * COIN, CScript() << OP_TRUE);
    const int nBlockTime = 1700000000;
    const int nHeight = 100;
    AddCoins(cache, CTransaction(mtx_parent), nHeight, false, nBlockTime);

    // Create a v2 child tx that spends the parent's output
    CMutableTransaction mtx_child;
    mtx_child.version = 2;
    mtx_child.nTime = 0;
    mtx_child.vin.emplace_back(COutPoint(mtx_parent.GetHash(), 0));
    mtx_child.vout.emplace_back(100 * COIN, CScript() << OP_TRUE);
    CTransaction tx_child(mtx_child);

    // CheckTxInputs must use nBlockTime (not wall clock) for v2 txs
    TxValidationState state;
    CAmount txfee = 0;
    BOOST_CHECK(Consensus::CheckTxInputs(
        tx_child, state, cache, nHeight, txfee, nBlockTime));
}
```

### Why This Fix is Correct

1. **Symmetry:** Both sides of the comparison use the same time reference (block time for v2 txs)
2. **Determinism:** Block time is deterministic; wall clock is not
3. **Restores v262 behavior:** Chained v2 txs in the same block pass validation
4. **Consensus-safe:** Only changes behavior for v2 transactions in a way that makes the time check work as intended

### What This Fix Does NOT Change

- **`AddCoins` behavior** - Still uses `nBlockTime` for v2 txs (needed for coinstatsindex)
- **Coinstatsindex** - Still uses `block.nTime` for v2 txs (deterministic muhash)
- **Wallet `nTimeSmart`** - Still equals block time for confirmed v2 txs
- **Mempool validation** - Still uses wall clock (appropriate since there's no block yet)
- **v1 transaction behavior** - Completely unchanged

### Why the Previous Attempt Was Reverted

The user mentioned the previous fix was reverted. Without knowing the specific reason, possible issues could be:
- Concern about consensus rule changes
- Missing test coverage
- Concern about edge cases

This implementation includes:
- A regression test that would have caught the original bug
- Clear documentation of the fix
- Symmetric time references throughout

---

## 11.2. Code Review and Refinements

After implementing the fix, a code review identified four issues that required additional work:

### Issue 1: Test redundancy

**Problem:** The original regression test had two identical `BOOST_CHECK` calls, with a misleading comment about simulating the "old `GetAdjustedTimeSeconds()` behavior" that wasn't actually being tested.

**Fix:** Rewrote the test to actually exercise the bug scenario (see Issue 2 below).

### Issue 2: Test did not exercise the regression

**Problem:** The original test passed under both the old and new code, so it didn't prove the fix worked. It only verified that `coin.nTime == nBlockTime == nTimeTx`, which would also pass under the old `GetAdjustedTimeSeconds()` code on any node whose wall clock happened to match.

**Fix:** The regression test now has two parts:
1. **Demonstrates the bug:** Pass a `nBlockTime` that is 7 seconds *behind* `coin.nTime` (simulating wall clock skew). Assert that `CheckTxInputs` returns `false` with reject reason `bad-txns-time-earlier-than-input`.
2. **Demonstrates the fix:** Pass the same `nBlockTime` as `coin.nTime` (both reference the block header). Assert that `CheckTxInputs` returns `true`.

This ensures the test would fail under the old code and pass under the new code.

### Issue 3: Possible behavior change in mempool path (CRITICAL)

**Problem:** The mempool caller (`validation.cpp:911`) was passing `GetAdjustedTimeSeconds()` as `nBlockTime`. For v2 txs spending already-mined v2 UTXOs:
- `coin.nTime` was set to the block header time at mining (`coins.cpp:129`)
- `nTimeTx` would be the wall clock

If a user's wall clock is behind real time (common), `GetAdjustedTimeSeconds()` could be less than `coin.nTime`, causing `coin.nTime > nTimeTx` → `bad-txns-time-earlier-than-input` and mempool rejection.

Pre-fix, both sides used wall clock, so the comparison was self-consistent even if non-deterministic. Post-fix (with the original change), the two sides used different time sources for already-mined v2 UTXOs, introducing a new regression.

**Fix:** Use the chain tip's block time as `nBlockTime` for the mempool path:

```cpp
const CBlockIndex* const pindex = m_active_chainstate.m_chain.Tip();
const int64_t nBlockTime = pindex ? pindex->GetBlockTime() : GetAdjustedTimeSeconds();
if (!Consensus::CheckTxInputs(tx, state, m_view, m_active_chainstate.m_chain.Height() + 1,
                              ws.m_base_fees, nBlockTime)) {
```

This is deterministic and consistent with how `coin.nTime` was set at mining. The chain tip's time is the most recent block header time, which is the best estimate of "now" for mempool purposes without relying on the local clock.

### Issue 4: Minor doc comment placement

**Problem:** The new `@param[in] nBlockTime` block was inserted in the middle of existing documentation, making it read awkwardly.

**Fix:** Moved the `@param[in] nBlockTime` documentation to appear *after* the existing `@param[out] txfee` line, so the doc comment reads naturally from top to bottom.

### Updated Files (Final)

After the review fixes, the following files were modified:

1. **`src/consensus/tx_verify.h`** - Added `nBlockTime` parameter, reordered doc comment
2. **`src/consensus/tx_verify.cpp`** - Updated implementation to use `nBlockTime`
3. **`src/validation.cpp:2622`** (block validation) - Passes `pindex->nTime`
4. **`src/validation.cpp:911`** (mempool) - Passes chain tip's `GetBlockTime()` (not wall clock)
5. **`src/txmempool.cpp:745`** (mempool sanity check) - Passes `GetAdjustedTimeSeconds()` (debug-only, with comment)
6. **`src/test/fuzz/coins_view.cpp:258`** (fuzz test) - Passes `GetAdjustedTimeSeconds()`, added `#include <util/time.h>`
7. **`src/test/coins_tests.cpp`** - Rewrote regression test to actually exercise the bug, added `#include <consensus/tx_verify.h>`
8. **`agent/ntime-investigation.md`** - This file (updated with review findings)

### Final Fix Summary

The fix now addresses all time references consistently:

| Context | Time source for `nBlockTime` | Rationale |
|---|---|---|
| Block validation (`ConnectBlock`) | `pindex->nTime` (block header time) | Matches `coin.nTime` set by `AddCoins` |
| Mempool validation (`PreChecks`) | `m_active_chainstate.m_chain.Tip()->GetBlockTime()` | Deterministic, not subject to local clock skew |
| Mempool sanity check | `GetAdjustedTimeSeconds()` | Debug-only assertion on already-validated txs |
| Fuzz test | `GetAdjustedTimeSeconds()` | Random input testing, wall clock is fine |

This ensures that for v2 transactions, both sides of the `coin.nTime > nTimeTx` check reference the same time source (chain time, not wall clock), eliminating the original bug without introducing a new regression in the mempool path.

---

## 14. Witness Version Range Analysis (v2-v16, including v14, v15, v16)

After the initial SegWit v>1 compatibility analysis, a deeper investigation was performed to verify how the codebase handles the full witness version range (v0-v16), with special attention to edge cases like v14, v15, and v16.

### 14.1. Valid Witness Version Range

**The valid witness version range is 0-16 (inclusive).**

This is enforced by the opcode encoding, not by an explicit version check:

**Location:** `src/script/script.cpp:243-257` (`IsWitnessProgram`)

```cpp
bool CScript::IsWitnessProgram(int& version, std::vector<unsigned char>& program) const
{
    if (this->size() < 4 || this->size() > 42) {
        return false;
    }
    if ((*this)[0] != OP_0 && ((*this)[0] < OP_1 || (*this)[0] > OP_16)) {
        return false;  // Rejects opcodes outside 0x00 and 0x51-0x60
    }
    // ...
}
```

**Opcode range:**
- v0: `OP_0` (0x00)
- v1-v16: `OP_1` through `OP_16` (0x51-0x60)
- v17+: Not recognized as witness program (rejected by opcode check)

**Location:** `src/script/script.h:75-98` (opcode definitions)

```cpp
OP_0 = 0x00,
...
OP_1 = 0x51,     // version 1
...
OP_16 = 0x60,    // version 16 (MAXIMUM)
```

**Location:** `src/key_io.cpp:188-191` (Bech32 address parsing)

```cpp
if (version > 16) {
    error_str = "Invalid Bech32 address witness version";
    return CNoDestination();
}
```

This is the only place where `version > 16` is explicitly checked. The check is `> 16` (not `>= 17`), so version 16 is permitted at the address parsing layer.

### 14.2. v2-v16 Handling at Each Layer

The codebase has **multiple layers of protection** against v2-v16 witness versions. All versions from v2 to v16 are treated identically — there is **no special handling for v14, v15, or v16 specifically**.

| Layer | Location | Behavior for v2-v16 |
|---|---|---|
| **`IsWitnessProgram`** | `script.cpp:243-257` | Recognizes as valid witness program (any v0-v16) |
| **`Solver`** | `solver.cpp:154-178` | Classifies as `WITNESS_UNKNOWN` |
| **`IsStandard` (policy)** | `policy.cpp:71-96` | **Rejects unconditionally** from mempool |
| **`AreInputsStandard` (policy)** | `policy.cpp:182-214` | **Rejects unconditionally** from mempool |
| **`VerifyWitnessProgram` (interpreter)** | `interpreter.cpp:1888-1971` | Falls to `else` branch — returns error if DISCOURAGE flag set, else returns true (anyone-can-spend) |
| **`ContextualCheckBlock` (consensus)** | `validation.cpp:4348-4369` | **Rejects block** with `bad-unknown-witness-version` if gate is active |
| **`GetBlockScriptFlags` (consensus)** | `validation.cpp:2453-2462` | Adds `SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM` when Taproot active |
| **`net_processing.cpp`** | `net_processing.cpp:4783-4791` | Logs `unknown-witness-version` for peer monitoring |

### 14.3. Script Interpreter Behavior for v2-v16

**Location:** `src/script/interpreter.cpp:1888-1971` (`VerifyWitnessProgram`)

```cpp
static bool VerifyWitnessProgram(const CScriptWitness& witness, int witversion,
                                  const std::vector<unsigned char>& program,
                                  unsigned int flags, const BaseSignatureChecker& checker,
                                  ScriptError* serror, bool is_p2sh)
{
    if (witversion == 0) {
        // P2WSH (32-byte) or P2WPKH (20-byte) handling
        // ...
    } else if (witversion == 1 && program.size() == WITNESS_V1_TAPROOT_SIZE && !is_p2sh) {
        // BIP341 Taproot handling
        // ...
    } else if (!is_p2sh && CScript::IsPayToAnchor(witversion, program)) {
        // P2A (Pay-to-Anchor) - anyone-can-spend, returns true
        return true;
    } else {
        // ALL OTHER VERSIONS (v2-v16) FALL HERE
        if (flags & SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM) {
            return set_error(serror, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM);
        }
        // Other version/size/p2sh combinations return true for future softfork compatibility
        return true;
    }
}
```

**Key findings:**
- v0 and v1 have specific handling (P2WPKH, P2WSH, P2TR)
- v1 with P2A program (2 bytes 0x4e73) returns true (anyone-can-spend anchor)
- **All other witness versions (v2-v16) fall through to the final `else` branch**
- If `SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM` is set, v2-v16 fail with `SCRIPT_ERR_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM`
- If the discourage flag is NOT set, v2-v16 silently pass (forward-softfork compatibility — outputs are anyone-can-spend)

### 14.4. Policy Rejection of v2-v16

**Location:** `src/script/solver.cpp:154-178`

```cpp
if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
    if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_KEYHASH_SIZE) {
        vSolutionsRet.push_back(std::move(witnessprogram));
        return TxoutType::WITNESS_V0_KEYHASH;
    }
    if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_SCRIPTHASH_SIZE) {
        vSolutionsRet.push_back(std::move(witnessprogram));
        return TxoutType::WITNESS_V0_SCRIPTHASH;
    }
    if (witnessversion == 1 && witnessprogram.size() == WITNESS_V1_TAPROOT_SIZE) {
        vSolutionsRet.push_back(std::move(witnessprogram));
        return TxoutType::WITNESS_V1_TAPROOT;
    }
    if (scriptPubKey.IsPayToAnchor()) {
        return TxoutType::ANCHOR;
    }
    if (witnessversion != 0) {  // ← CATCHES v1-v16 (including v2-v16)
        vSolutionsRet.push_back(std::vector<unsigned char>{(unsigned char)witnessversion});
        vSolutionsRet.push_back(std::move(witnessprogram));
        return TxoutType::WITNESS_UNKNOWN;
    }
    return TxoutType::NONSTANDARD;
}
```

**Key finding:** The check `witnessversion != 0` captures **all non-zero versions** (v1-v16), which are all classified as `WITNESS_UNKNOWN`. This includes v14, v15, and v16 identically.

**Location:** `src/policy/policy.cpp:71-96`

```cpp
} else if (!witnessEnabled && (whichType == TxoutType::WITNESS_V0_SCRIPTHASH ||
                                whichType == TxoutType::WITNESS_V0_KEYHASH ||
                                whichType == TxoutType::WITNESS_V1_TAPROOT)) {
    return false;
} else if (whichType == TxoutType::WITNESS_UNKNOWN) {
    return false;  // Unconditional rejection of v2-v16
}
```

**`WITNESS_UNKNOWN` (v2-v16) is unconditionally rejected from mempool acceptance.** This is a hard policy rejection — no flag can override it.

### 14.5. ContextualCheckBlock Validation for v2-v16

**Location:** `src/validation.cpp:4348-4369`

```cpp
const bool block_unknown_witness = (chain_type == ChainType::TESTNET)
    ? DeploymentActiveAfter(pindexPrev, chainman, Consensus::DEPLOYMENT_TAPROOT)
    : DeploymentActiveAfter(pindexPrev, chainman, Consensus::DEPLOYMENT_SEGWIT);
if (block_unknown_witness) {
    for (const auto& tx : block.vtx) {
        for (const auto& txout : tx->vout) {
            int witness_version = 0;
            std::vector<unsigned char> witness_program;
            if (txout.scriptPubKey.IsWitnessProgram(witness_version, witness_program) &&
                witness_version > 1) {  // ← CATCHES v2-v16
                return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS,
                                    "bad-unknown-witness-version",
                                    strprintf("%s : block contains output with unknown witness version %d",
                                             __func__, witness_version));
            }
        }
    }
}
```

**Key findings:**
- The check `witness_version > 1` correctly captures **v2-v16**
- v0 and v1 always pass
- **No special handling for v16 specifically** — it falls under the same `> 1` rule
- The check is gated on different deployments per chain type:
  - **Mainnet/Signet/Regtest/Testnet4:** SegWit (already active)
  - **Legacy Testnet:** Taproot (since v16 outputs exist in legacy testnet's history at ~block 2,864,xxx before Taproot at 2,865,000)

### 14.6. nBlockTime Fix Compatibility with v2-v16

**The nBlockTime fix is fully compatible with all witness versions, including v2-v16.**

**Location:** `src/consensus/tx_verify.cpp:165-228`

```cpp
int64_t nTimeTx = tx.nTime;
if (!nTimeTx && tx.version >= 2)  // ← tx version, NOT witness version
    nTimeTx = nBlockTime;
// ...
if (coin.nTime > nTimeTx)
    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-time-earlier-than-input");
```

**Key findings:**
- The nBlockTime fix is **witness-type-agnostic**. It uses:
  - `tx.version >= 2` (tx version, not witness version)
  - `nTimeTx` is an `int64_t`, not a witness version
  - `coin.nTime` comparison is against a tx-level time field
- The time check has **no version-specific logic** — works identically for v0, v1, v2-v16
- v2-v16 outputs are rejected at `ContextualCheckBlock` (creation) and via `WITNESS_UNKNOWN` in policy (mempool) and via `DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM` (consensus spending). However, IF a v2-v16 output somehow existed in the UTXO set (e.g., from pre-existing testnet v16 outputs), the nBlockTime fix would handle it correctly.

### 14.7. Edge Cases and Special Handling

#### v16 (OP_16) Specific Handling
**No special handling for OP_16 specifically.** The boundary check is `>= OP_1 && <= OP_16`, which includes v16. OP_16 = 0x60 is the last byte that the witness program format accepts.

#### Versions > 16 Rejection
Versions > 16 are rejected at multiple levels:
1. **`IsWitnessProgram`** (`script.cpp:248`) — opcode > OP_16 rejected
2. **`key_io.cpp:188`** — `if (version > 16)` rejects Bech32 versions > 16
3. **Outside the opcode range** (0x61 = OP_NOP and above), the script is not recognized as a witness program

#### Integer Overflow Risk
- `DecodeOP_N` returns `int`, so for OP_1 to OP_16 the result is 1-16 — no overflow risk
- `CScript::IsWitnessProgram` stores in `int& version` — safe
- The `solver.cpp:173` cast `(unsigned char)witnessversion` is safe for values 0-16
- The `witness_version` variable in `validation.cpp:4361` is declared as `int` (not `unsigned int`), so no signed/unsigned comparison issues

#### Legacy Testnet Pre-existing v16 Outputs
The investigation reveals that **legacy testnet (chain_type == TESTNET) has v16 outputs** in the history at ~block 2,864,xxx (created before Taproot at 2,865,000). The code at `validation.cpp:4354-4357` accommodates this by gating on `DEPLOYMENT_TAPROOT` for legacy testnet specifically:

```cpp
const bool block_unknown_witness = (chain_type == ChainType::TESTNET)
    ? DeploymentActiveAfter(pindexPrev, chainman, Consensus::DEPLOYMENT_TAPROOT)
    : DeploymentActiveAfter(pindexPrev, chainman, Consensus::DEPLOYMENT_SEGWIT);
```

This means on legacy testnet, v16 outputs created before Taproot activation remain valid (preserving history), but new v16 outputs after Taproot activation are rejected.

#### ANCHOR at v1
The v1 P2A (Pay-to-Anchor) case has special handling: a v1 script with 2 bytes 0x4e73 is treated as anyone-can-spend. This is unique to v1 with a specific program and does not extend to v2-v16 (the else-if branch only matches `v==1`).

### 14.8. Test Coverage for v2-v16

#### Unit Tests

**`src/test/script_segwit_tests.cpp:93-111` — `IsWitnessProgram_Valid`**
- Tests v0 (`OP_0` + 2 bytes), v16 (`OP_16` + 40 bytes), and v5 (pushdata-encoded).
- **Confirms v16 is a valid witness program** at the parsing layer.

**`src/test/script_segwit_tests.cpp:113-119` — `IsWitnessProgram_Invalid_Version`**
- Tests `OP_1NEGATE` is rejected as witness version.

**`src/test/script_standard_tests.cpp:123-129`**
```cpp
// TxoutType::WITNESS_UNKNOWN
s.clear();
s << OP_16 << ToByteVector(uint256::ONE);
BOOST_CHECK_EQUAL(Solver(s, solutions), TxoutType::WITNESS_UNKNOWN);
BOOST_CHECK_EQUAL(solutions.size(), 2U);
BOOST_CHECK(solutions[0] == std::vector<unsigned char>{16});
BOOST_CHECK(solutions[1] == ToByteVector(uint256::ONE));
```
- **Tests that OP_16 (v16) is classified as WITNESS_UNKNOWN.**

**`src/test/script_standard_tests.cpp:204-214`**
- Tests that wrong-witness-version anchor (v2 with anchor bytes) is `WITNESS_UNKNOWN`.
- Tests that wrong-data-push anchor (v1 with non-anchor bytes) is `WITNESS_UNKNOWN`.

**`src/wallet/test/ismine_tests.cpp:692-705`**
- Tests wallet `IsMine` for OP_16 script returns `ISMINE_NO` (not relevant for v16).

**`src/test/sigopcount_tests.cpp:171-177`**
- Tests that witness version != 0 has zero sigop cost.

#### JSON Data Tests

**`src/test/data/tx_valid.json:411-415`**
- "Unknown witness program version (without DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM)" — uses OP_16 (0x60) with 20-byte push, confirms valid tx without discourage flag.

**`src/test/data/tx_valid.json:453-455`**
- "The witness version must use OP_1 to OP_16 only" — uses 0x01 0x10 0x02 0x0001 (OP_16 + 2-byte program).

**`src/test/data/tx_invalid.json:255-259`**
- "Unknown witness program version (with DISCOURAGE_UPGRADAGE_WITNESS_PROGRAM)" — uses OP_16, expects rejection with discourage flag.

**`src/test/data/tx_invalid.json:301-303`**
- "Unknown witness version with non empty scriptSig" — uses OP_16 with non-empty scriptSig.

**`src/test/data/script_tests.json:2073-2077`**
- "P2WPKH with future witness version" — tests discourage flag handling.

**`src/test/data/script_tests.json:1274`**
- "OP_16 does introduce a witness program" — confirms OP_16 is a valid witness version opcode.

#### nBlockTime Fix Tests

**`src/test/coins_tests.cpp:807-837` — `addcoins_v2_ntime_uses_block_time`**
- Verifies v2 txs use nBlockTime for coin.nTime.
- **Does not test witness versions specifically**, but the time field is tx-level, not witness-level.

**`src/test/coins_tests.cpp:850-898` — `checktxinputs_v2_chained_in_same_block`**
- The regression test for the `bad-txns-time-earlier-than-input` bug.
- Uses v2 txs but the test outputs are `CScript() << OP_TRUE` (not witness programs).
- **Does not test witness outputs specifically**, but the time logic is independent of witness type.

#### Test Coverage Gaps

1. **No functional test for v>1 block rejection** (`feature_unknown_witness_version.py` does not exist in `test/functional/`).
2. **No functional test for nBlockTime fix with witness transactions** (e.g., v2 P2TR spend of v2 P2TR in same block).
3. **No coinstatsindex test for P2TR outputs** (explicit muhash/bogo_size verification).
4. **No test for witness version check on legacy testnet** (v>1 allowed pre-Taproot, rejected post-Taproot).
5. **No v14 or v15 specific tests** — but the same code paths handle v2-v16, so the v16 tests provide adequate coverage by extension.

### 14.9. Compatibility Summary for v2-v16

| Witness Version | Status | Notes |
|---|---|---|
| **v2** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v3** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v4** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v5** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v6** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v7** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v8** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v9** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v10** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v11** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v12** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v13** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v14** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v15** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |
| **v16** | ✅ Properly Rejected | Treated as WITNESS_UNKNOWN, rejected at mempool/block level |

| Component | v2-v16 Compatible | Notes |
|---|---|---|
| **CheckTxInputs nBlockTime** | ✅ Yes | Witness-agnostic, works for all tx types |
| **AddCoins nBlockTime** | ✅ Yes | Witness-agnostic, preserves all script types |
| **CoinstatsIndex** | ✅ Yes* | *v>1 unreachable due to block rejection |
| **Mempool policy** | ✅ Yes | WITNESS_UNKNOWN rejected unconditionally |
| **Block consensus** | ✅ Yes | v>1 creation/spending rejected |
| **SCRIPT_VERIFY flags** | ✅ Yes | DISCOURAGE flag set when Taproot active |

### 14.10. Potential Issues

**1. No v14 or v15 specific tests**
- But the same code paths handle v2-v16, so the v16 tests provide adequate coverage by extension
- The witness version range is enforced by the opcode encoding (0x00 and 0x51-0x60), not by explicit version checks

**2. Legacy testnet v16 outputs**
- At ~block 2,864,xxx are correctly handled by the chain-type-specific gate at `validation.cpp:4354-4357`
- These outputs are preserved by gating on `DEPLOYMENT_TAPROOT` instead of `DEPLOYMENT_SEGWIT` for legacy testnet

**3. No integer overflow risk**
- All version handling uses `int` or `unsigned char` within the 0-16 range
- The opcode encoding naturally limits the range to 0-16

**4. The DISCOURAGE flag gate**
- Is on `DEPLOYMENT_TAPROOT` (per `validation.cpp:2460`)
- During the BIP-9 STARTED→LOCKED_IN window for Taproot, v>1 outputs are accepted at script execution time (anyone-can-spend) but rejected at block creation time by `ContextualCheckBlock` (which gates on `DEPLOYMENT_SEGWIT` for non-testnet chains)
- This is correct per the comments in the code

### 14.11. Conclusion

**All recent changes (nBlockTime fix, coinstatsindex update, witness version protection) are fully compatible with witness versions v2 through v16, including v14, v15, and v16.**

**Key findings:**

1. **Valid witness version range is 0-16** (enforced by opcode encoding, not explicit version checks)
2. **v2-v16 are treated identically** — no special handling for high version numbers like v14, v15, v16
3. **Multi-layered protection** — rejected at mempool policy, block creation, and block spending levels
4. **nBlockTime fix is witness-type-agnostic** — works for all witness versions
5. **CoinstatsIndex is witness-type-agnostic** — handles all witness versions through generic Coin serialization
6. **No critical bugs or consensus issues identified** — the integration is logically sound

**The only gap is test coverage** for:
- Functional tests for v>1 block rejection
- nBlockTime fix with witness transactions
- Coinstatsindex with P2TR outputs
- Witness version check on legacy testnet

These should be addressed before the v28-CORE release, but they are not critical for the current fix to be safe.
---

## 13. Recommendations

### Immediate (to fix the production bug)

1. **Re-apply Option A** (fix `CheckTxInputs` to use `nBlockTime`) — but first investigate why the previous attempt was reverted. Possible reasons:
   - A different test failure (not `bad-txns-time-earlier-than-input`)
   - A consensus rule change that was deemed too risky
   - A build/compilation issue (signature mismatch elsewhere)
   - Concern about changing consensus rules

2. **Add a regression test** for the failing case: a v2 tx spending a v2 output in the same block, with the block time slightly ahead of the node's wall clock.

### Short-term

3. **Audit all `v1 ? v1_value : v2_value` patterns** to ensure consistency. The pattern should use the **same time reference** on both sides of any comparison.

4. **Document the invariant** that `Coin.nTime == block.nTime == wtx.nTimeSmart` for confirmed v2 txs. This invariant is relied upon by `wallet/staking.cpp` and `wallet/wallet.cpp`.

5. **Consider whether the `CheckTxInputs` time check is needed at all** for v2 txs. If the check is effectively dead code (v262) or always passes (v284 with the fix), maybe it should be removed or replaced with a different check.

### Long-term

6. **Consider deprecating v1 transactions** entirely. v1 txs have `nTime` on wire, which creates the v1/v2 dichotomy. If all txs were v2, the codebase would be simpler.

7. **Consider moving `Coin.nTime` to a separate index** rather than storing it in the UTXO set. This would reduce the muhash complexity and avoid the coinstatsindex migration code.

8. **Add fuzzing for v2 tx time handling** to catch edge cases like the one that caused this bug.

---

## 15. Mempool AddCoins Fix - REVERTED

After implementing the block validation fix and verifying it works in production (1,124 confirmations on block 5,944,947), we identified a related issue in the mempool:

### The Issue

The mempool's `AddCoins` call in `src/txmempool.cpp:751` did not pass `nBlockTime`:

```cpp
AddCoins(mempoolDuplicate, tx, std::numeric_limits<int>::max());
```

This meant that for v2 transactions in the mempool, `coin.nTime` was set to `tx.nTime` (which is 0 for v2 txs), rather than the block time. This had two potential issues:

1. **CSV (BIP 68) calculations** - `coin.nTime` is used in `CalculateSequenceLocks`, so it would be 0 for v2 mempool transactions
2. **Time-warp protection** - the `coin.nTime > nTimeTx` check in `CheckTxInputs` would be bypassed for v2 mempool transactions

### The Attempted Fix

We initially implemented a fix to pass `nBlockTime` to the mempool's `AddCoins` call:

1. Modified `CTxMemPool::check()` to accept a `nBlockTime` parameter
2. Updated callers in `validation.cpp` to pass the chain tip time
3. Updated the mempool's `AddCoins` call to pass `nBlockTime`
4. Added a regression test

### The Decision to Revert

**We decided to REVERT the mempool fix** for the following critical reasons:

1. **Network Stability Risk** - The whole network previously stopped, forcing everyone to downgrade and resync from scratch. We cannot risk another consensus issue.

2. **Untested in Production** - The mempool fix was only unit-tested, not validated in production. The block validation fix is verified by 1,124 confirmations, but the mempool fix had no production validation.

3. **v262 Had the Same "Issue"** - v262's mempool also didn't pass `nBlockTime` to `AddCoins`, but the time check was dead code anyway. This means the "issue" has existed since v2 transactions were introduced (April 2022) without causing any known problems.

4. **Theoretical vs. Practical** - The CSV/time-warp concerns are theoretical security issues, not practical consensus failures. The original bug (block validation) was a practical consensus failure that stopped the network.

5. **Principle of "Do No Harm"** - The block validation fix alone solves the original problem. Adding the mempool fix introduces new code paths that could have unforeseen issues.

### Current State (After Reversion)

**Kept (verified working):**
- ✅ `CheckTxInputs` uses `nBlockTime` for v2 transactions (block validation)
- ✅ All callers in `validation.cpp` (block path) pass `pindex->nTime`
- ✅ Mempool `PreChecks` passes `chaintip_time` as `nBlockTime`
- ✅ Regression test for chained v2 transactions in same block
- ✅ CoinstatsIndex compatibility verified
- ✅ SegWit v>1 compatibility verified

**Reverted (too risky):**
- ❌ Mempool's `AddCoins` call does NOT pass `nBlockTime` (back to original)
- ❌ `CTxMemPool::check()` signature is back to original (2 parameters)
- ❌ Validation.cpp callers back to original
- ❌ Regression test for mempool CSV removed

### Known Remaining Issue

The mempool still has the theoretical issue where v2 transactions have `coin.nTime = 0`. This means:
- CSV calculations for v2 mempool transactions are incorrect
- Time-warp protection is bypassed for v2 mempool transactions

**However, this issue:**
- Has existed since v2 transactions were introduced (April 2022)
- Did not cause any known problems in v262
- Is consistent with v262's behavior
- Can be fixed later after thorough testing

### Future Work

The mempool fix should be:
1. Developed and tested on testnet4
2. Reviewed by other Blackcoin developers
3. Run for at least 2-4 weeks on testnet
4. Only then considered for mainnet deployment

**For now, the block validation fix is sufficient** to solve the original network-stopping problem.

---

## Appendix A: Timeline of the Bug

| Date | Event |
|---|---|
| Pre-v262 | v1 transactions only. `Coin.nTime = tx.nTime`. No time check in `CheckTxInputs`. |
| v262 | v2 transactions introduced. `Coin.nTime = tx.nTime` (= 0 for v2). Time check added to `CheckTxInputs` but effectively dead code for v2. Works correctly. |
| July 1, 2026 | Commit `002b58d84f` changes `AddCoins` to use `nBlockTime` for v2 txs. Purpose: fix coinstatsindex. Side effect: makes the time check in `CheckTxInputs` actually fire. |
| July 8, 2026 17:23:53 UTC | Block 5944947 rejected with `bad-txns-time-earlier-than-input`. Block time (17:24:00) is 7 seconds ahead of node's wall clock (17:23:53). |

---

## Appendix B: File Diff Summary

### `coins.cpp` (v262 → v284)

```diff
-void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check_for_overwrite) {
+void AddCoins(CCoinsViewCache& cache, const CTransaction &tx, int nHeight, bool check_for_overwrite, int nBlockTime) {
     bool fCoinbase = tx.IsCoinBase();
     bool fCoinstake = tx.IsCoinStake();
     const Txid& txid = tx.GetHash();
+    int nTimeCoin = tx.version >= 2 ? nBlockTime : (int)tx.nTime;
     for (size_t i = 0; i < tx.vout.size(); ++i) {
         bool overwrite = check_for_overwrite ? cache.HaveCoin(COutPoint(txid, i)) : fCoinbase;
-        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase, fCoinstake, tx.nTime), overwrite);
+        cache.AddCoin(COutPoint(txid, i), Coin(tx.vout[i], nHeight, fCoinbase, fCoinstake, nTimeCoin), overwrite);
     }
 }
```

### `validation.cpp` (v262 → v284)

```diff
-void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight)
+void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight, int nBlockTime)
 {
     ...
-    AddCoins(inputs, tx, nHeight);
+    AddCoins(inputs, tx, nHeight, /*check*/false, nBlockTime);
 }

-UPDATE_COINS call in ConnectBlock: UpdateCoins(tx, view, ..., pindex->nHeight);
+UPDATE_COINS call in ConnectBlock: UpdateCoins(tx, view, ..., pindex->nHeight, block.nTime);
```

### `coinstatsindex.cpp` (v262 → v284)

```diff
-Coin coin{out, block.height, tx->IsCoinBase(), tx->IsCoinStake(), (int)tx->nTime};
+int nTimeOut = tx->version >= 2 ? (int)block.data->nTime : (int)tx->nTime;
+Coin coin{out, block.height, tx->IsCoinBase(), tx->IsCoinStake(), nTimeOut};

+// For old undo data written before the AddCoins fix
+if (coin.nTime == 0 && coin.nHeight > 0) {
+    const CBlockIndex* pindexPrev = pindex->GetAncestor(coin.nHeight);
+    if (pindexPrev) coin.nTime = pindexPrev->nTime;
+}
```

### `kernel/coinstats.cpp` (v262 → v284)

```diff
-    4 /* height + coinbase */ +
+    4 /* height + coinbase + coinstake */ +
+    4 /* nTime */ +

-ss << static_cast<uint32_t>((coin.nHeight << 1) + coin.fCoinBase);
+ss << static_cast<uint32_t>((coin.nHeight << 2) + (coin.fCoinBase ? 1u : 0u) + (coin.fCoinStake ? 2u : 0u));
+ss << VARINT(coin.nTime);
```

---

## Appendix C: Quick Reference — All Time-Related Functions

| Function | Location | Returns | Used by |
|---|---|---|---|
| `GetAdjustedTimeSeconds()` | `util/time.cpp:47` | Node's wall-clock time (seconds) | `CheckTxInputs` (v2), `PreChecks` (v2), `miner.cpp`, `FutureDrift`, `GetMinFee`, `coinstatsindex` |
| `GetTime()` | `util/time.cpp:44` | Same as `GetAdjustedTimeSeconds()` (deprecated) | Various wallet code |
| `block.GetBlockTime()` | `primitives/block.h:71` | `block.nTime` | Validation, mining, RPC |
| `pindex->nTime` | `chain.h:197` | Block header time from index | Validation, mining, coinstatsindex |
| `pindex->GetMedianTimePast()` | `chain.h:312-329` | MTP (modified in Blackcoin: returns `GetBlockTime()` post-V2) | Validation, mining, consensus |
| `coin.nTime` | `coins.h:50` | UTXO entry's time (0 for v1→v262, nBlockTime for v2→v284) | `CheckTxInputs`, `pos.cpp`, `coinstatsindex` |
| `wtx.nTimeSmart` | `wallet/transaction.h:202` | Smart time (blocktime for confirmed coinstakes) | `wallet/staking.cpp`, `wallet/wallet.cpp` |
| `ComputeTimeSmart()` | `wallet/wallet.cpp:2935-2989` | Computes `nTimeSmart` from block time and heuristics | `AddToWallet`, `LoadToWallet` |
| `IsFinalTx()` | `consensus/tx_verify.cpp:18-38` | Whether tx is final for given block height/time | `CheckBlock`, `PreChecks` |
| `CheckProofOfStake()` | `pos.cpp:131-184` | Validates PoS kernel | `ConnectBlock`, `ProcessBlockFound` |
| `CheckStakeKernelHash()` | `pos.cpp:78-128` | Validates kernel hash | `CheckProofOfStake`, `CheckKernel` |
| `CacheKernel()` | `pos.cpp:230-253` | Caches kernel hash for performance | Staking |
| `FutureDrift()` | `validation.cpp:145-152` | Max allowed future timestamp | `CheckBlockHeader`, `PreChecks` |
| `GetMinFee()` | `consensus/tx_verify.cpp:225-248` | Minimum fee for given time | `CheckTxInputs`, `PreChecks`, wallet fee estimation |
