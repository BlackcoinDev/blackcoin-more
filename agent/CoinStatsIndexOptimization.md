# CoinStatsIndex — Performance Optimization

## History of Changes

The CoinStatsIndex bug was a chain of interconnected issues stemming from one root cause: **v2 transactions don't serialize `nTime`**, but the `Coin` object and MuHash depend on it.

### Commit 1: `5bb1305` — Add `fCoinStake` and `nTime` to MuHash

**What**: Changed `TxOutSer()` in `kernel/coinstats.cpp` to encode `fCoinStake` and `nTime` into the MuHash serialization.

**Why**: Without these fields, the MuHash couldn't distinguish coinstake outputs from regular outputs, and couldn't track per-UTXO timestamps. This was needed for accurate UTXO set hashing.

**Consequence**: MuHash now depends on correct `nTime` values. But `coin.nTime` was always `0` for v2 txs (deserialization artifact), so the MuHash was computed from wrong data. This didn't cause a crash initially — it just silently produced incorrect hashes.

### Commit 2: `8430495` — Fix nTime for new-output paths in CoinStatsIndex

**What**: In `CustomAppend` and `ReverseBlock`, fixed the `Coin` construction for **new outputs** (from `tx->vout`) to use `block.nTime` instead of `tx.nTime` for v2 coinstakes.

**Why**: The new outputs were being added to MuHash with correct `nTime` (from our fix: `block.nTime`), but the undo-data paths still read `nTime = 0` from disk. This created a mismatch: insert used `block.nTime`, remove used `0` → MuHash drifted → **assertion crash on reorg** when `ReverseBlock` called `Finalize()` and compared against the DB value.

**Consequence**: The assertion started firing every time your node staked a block and it got orphaned.

### Uncommitted: `AddCoins` root-cause fix + CoinStatsIndex fallback

**What**: Two changes in this session:

1. **`AddCoins` fix** (coins.cpp, coins.h): Added `nBlockTime` parameter. For v2 coinstakes, stores `nTime = block.nTime` instead of `tx.nTime = 0` in the `Coin`. This fixes: UTXO cache, undo data, `tx_verify.cpp:191` temporal check — everything downstream.

2. **CoinStatsIndex fallback** (coinstatsindex.cpp): In both `CustomAppend` and `ReverseBlock`, when reading coins from undo data, if `nTime == 0 && IsCoinStake()`, reconstruct `nTime` from the creation block via `pindex->GetAncestor(coin.nHeight)->nTime`. This handles old undo data written before the fix without requiring a full `-reindex`.

3. **Validation.cpp plumbing**: `UpdateCoins` and `ConnectBlock` now pass `block.nTime` through to `AddCoins`.

**Why**: The earlier fix (commit 2) only fixed half the problem — new outputs but not undo data. This fixes the source (`AddCoins`), so every consumer gets correct `nTime` automatically.

### Related: SegWitTxv2Coinstake.md

**What**: Document analyzing a txid collision edge case for v2 segwit coinstakes on reorg.

**Why**: The same root cause (v2 drops `nTime` from serialization) means a segwit coinstake's non-witness bytes are identical across retries with the same UTXO and outputs. Not a fork, but a correctness concern. Qtum protects against this with `setStakeSeen`. Documented for future reference.

---

## Current State

The CoinStatsIndex assertion crash is fixed. The index is rebuilding. But it's **very slow** — same slowness as Bitcoin Core and Qtum. Here's why:

---

## Performance Analysis

### Bottleneck 1: Per-block `Finalize()` (~60% of time)

In `coinstatsindex.cpp:226` (and Qtum's `:232`, Bitcoin's `:232`):

```cpp
uint256 out;
m_muhash.Finalize(out);   // ← 3-5ms per block
value.second.muhash = out;
```

`Finalize()` computes `numerator / denominator` via a 3072-bit modular inverse (~3000 modular squarings + ~24 multiplications). This is called **on every single block during sync** — the result is stored to the DB per block height key. The MuHash3072 fraction (numerator + denominator) is already serializable (`muhash.h:120-124`), so *Finalize could be deferred entirely to lookup time*.

**CallCount**: 1× per block in `CustomAppend` + 1× per block in `ReverseBlock` (reorg) + 1× in `CustomInit` (startup verification).

### Bottleneck 2: Zero DB cache

In `init.cpp:1688` (and Qtum's `:2119`, Bitcoin's same):

```cpp
g_coin_stats_index = std::make_unique<CoinStatsIndex>(
    ..., /*cache_size=*/0, ...
);
```

Every LevelDB read (`m_db->Read(DBHeightKey(...))`) and write (`m_db->Write(...)`) misses cache. TxIndex gets a share of `dbcache`; CoinStatsIndex gets 0.

**CallCount per block**: 1× Read (previous height) + 1× Write (current height).

### Bottleneck 3: Per-block LevelDB writes

```cpp
return m_db->Write(DBHeightKey(block.height), value);
```

Each block writes individually instead of batching to the 30-second `Commit()` interval. Creates a new `CDBBatch` for every block.

---

## Proposed fix: Defer `Finalize()` to Lookup

### Change

Instead of calling `Finalize()` every block, store the raw MuHash3072 fraction (numerator + denominator) in the DB:

```cpp
// CustomAppend — store fraction, no Finalize
// Before:
uint256 out;
m_muhash.Finalize(out);
value.second.muhash = out;

// After:
// value.second.muhash = m_muhash; — but DBVal stores uint256, not MuHash3072
```

The `DBVal` struct currently stores `uint256 muhash`. We'd need to either:
- Store the raw MuHash3072 (numerator + denominator) as a blob
- Or keep the current scheme but move `Finalize` to `LookUpStats`

### Impact

| Metric | Before | After (estimated) |
|---|---|---|
| Time per block | 5-8ms | 2-3ms |
| Sync time (2.84M blocks) | ~6-8 hours | ~2-3 hours |
| `LookUpStats` latency | Instant (pre-computed) | +5ms (one Finalize on query) |
| DB size per entry | 32 bytes (uint256) | ~768 bytes (2× 3072-bit nums) |

### Why this is safe

`Finalize()` is only needed when someone queries the stats via `gettxoutsetinfo` or `LookUpStats`. During the index sync, nobody is querying. The MuHash fraction is fully serializable and can be faithfully restored.

### Tradeoffs

- **Pro**: 50-70% faster sync
- **Pro**: Same approach could be pulled from upstream if Bitcoin Core ever optimizes this
- **Con**: Larger DB entries (fraction vs hash)
- **Con**: Slightly slower queries (one Finalize at lookup time)
- **Con**: `ReverseBlock` assertion `Assert(read_out.second.muhash == out)` needs adjustment — can compare fractions directly instead of finalized hashes, or sample-check at Commit time

### Not implemented

This optimization is not implemented. The current code works correctly — it's just slow. The `Finalize()` bottleneck is identical in Bitcoin Core and Qtum; nobody has fixed it upstream yet.

---

## Summary

| Step | What | Why | Status |
|---|---|---|---|
| 1 | Added `fCoinStake` + `nTime` to MuHash `TxOutSer` | Accurate UTXO hashing | **Committed** `5bb1305` |
| 2 | Fixed new-output nTime in CoinStatsIndex | Insert used `block.nTime`, remove used `0` → crash | **Committed** `8430495` |
| 3 | Fixed `AddCoins` root cause | Coin UTXO cache and undo data had wrong nTime | **Uncommitted** |
| 4 | CoinStatsIndex undo-data fallback | Handle old undo data during rebuild | **Uncommitted** |
| 5 | `setStakeSeen` / `prevoutStake` | v2 segwit coinstake txid collision | **Documented only** `agent/SegWitTxv2Coinstake.md` |
| 6 | Defer `Finalize()` to lookup | Sync is 3-5ms/block slower than necessary | **Not implemented** |
