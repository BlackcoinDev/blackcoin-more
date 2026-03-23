# Stake Cache Documentation

> **BLACKCOIN-SPECIFIC FEATURE**
>
> The stake cache is originally from Qtum but Blackcoin More includes significant enhancements for debugging and monitoring.

## Overview

The stake cache optimizes Proof-of-Stake (PoS) kernel checking by caching UTXO metadata in memory, avoiding expensive disk I/O during staking.

## Origin

```
Stake cache by Qtum
Copyright (c) 2016-2018 The Qtum developers
```

Originally ported from [Qtum](https://github.com/qtumproject/qtum/blob/master/src/wallet/stake.cpp).

---

## How It Works

### Without Cache (Slow Path)

```
For each UTXO:
  1. view.GetCoin()          → Disk read
  2. pindexPrev->GetAncestor() → Chain traversal
  3. CheckStakeKernelHash()   → Hash calculation
```

### With Cache (Fast Path)

```
For each UTXO:
  1. cache.find(outpoint)     → Memory lookup (O(log n))
  2. CheckStakeKernelHash()   → Hash calculation
```

---

## Data Structure

**File:** `src/pos.h`

```cpp
struct CStakeCache {
    uint32_t blockFromTime;  // Timestamp of block containing UTXO
    CAmount amount;          // Value in satoshis
};
```

**Stored in:** `CWallet::stakeCache` (std::map<COutPoint, CStakeCache>)

---

## Cache Lifecycle

### 1. Population

**Location:** `src/wallet/staking.cpp:283-297`

```cpp
if (gArgs.GetBoolArg("-stakecache", node::DEFAULT_STAKE_CACHE)) {
    for (const auto& pcoin : setCoins) {
        COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
        CacheKernel(wallet.stakeCache, prevoutStake, pindexPrev, view);
    }
}
```

### 2. Usage

**Location:** `src/pos.cpp:172-218`

```cpp
bool CheckKernel(..., const std::map<COutPoint, CStakeCache>& cache) {
    auto it = cache.find(prevout);
    if (it != cache.end()) {
        // Cache HIT - use cached values
        return CheckStakeKernelHash(..., it->second.blockFromTime, it->second.amount, ...);
    }
}
```

### 3. Clearing

**Condition:** `cache.size() > setCoins.size() + 100`

When the cache has 100+ more entries than current stakeable UTXOs (indicating stale entries from spent coins), it is cleared entirely.

---

## Qtum vs Blackcoin More Comparison

| Feature | Qtum | Blackcoin More |
|---------|------|----------------|
| **Cache population** | ✅ | ✅ Same |
| **Cache usage in CheckKernel** | ✅ | ✅ Same |
| **Cache clear condition** | `size > setCoins + 100` | ✅ Same |
| **Reorg protection** | ✅ Re-validate without cache | ✅ Same |
| **RPC stats** | ❌ None | ✅ `getstakinginfo.stakecache` with `staked`, `lookups`, `efficiency` |
| **Debug logging** | ❌ None | ✅ Cache HIT/MISS/FLUSH |
| **Shutdown stats** | ❌ None | ✅ Logged at shutdown |
| **Address in log** | ❌ None | ✅ Shows UTXO address |
| **Efficiency tracking** | ❌ None | ✅ 10-minute rolling average |

---

## Blackcoin-Specific Enhancements

### 1. Statistics Counters

**File:** `src/wallet/wallet.h:807-813`

```cpp
std::atomic<uint64_t> m_stakecache_hits{0};                  // Times kernel found (Lottery win)
std::atomic<uint64_t> m_stakecache_lookups{0};               // Total internal cache lookups
std::atomic<uint64_t> m_stakecache_cache_misses{0};          // Times data not in cache (Disk Read)
std::atomic<uint64_t> m_stakecache_blocks{0};                // Blocks successfully staked using cache
std::atomic<uint64_t> m_stakecache_flushes{0};               // Times cache was cleared
std::atomic<bool> m_stakecache_initial_load_complete{false}; // Gate stats until warm
std::atomic<double> m_stakecache_efficiency_avg{0.0};        // Average efficiency trend
```

### 2. RPC Extension

**Command:** `getstakinginfo`

**File:** `src/wallet/rpc/staking.cpp:51-138`

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

| Field | Description |
| ----- | ----------- |
| `size` | Current number of entries in cache |
| `staked` | Total kernels found (lottery wins) via cache |
| `lookups` | Total internal cache lookups performed |
| `cache_misses` | Times data was not in cache (disk read required) |
| `efficiency` | Current cache efficiency percentage (staked / lookups * 100) |
| `efficiency_avg` | Rolling average efficiency (10-minute window) |
| `blocks` | Blocks successfully staked using cached data |
| `flushes` | Times the cache was cleared |
| `last_flush_reason` | Why cache was last flushed: `size_limit`, `manual`, `shutdown`, `cleanup` |
| `time_saved_ms` | Estimated time saved by cache (milliseconds) |

When `stakecache=0`:

```json
{
  "stakecache": {
    "enabled": false
  }
}
```

### 3. Debug Logging

Enable with: `-debug=coinstake`

**Cache HIT:**

```
[coinstake] CheckKernel: cache HIT for abc123...:2 (B7xYzQ...)
```

**Cache FLUSH:**

```
[coinstake] StakeCache: FLUSH - cache too large (1650 entries > 1500 UTXOs + 100 buffer)
```

**Shutdown Stats:**

```
[coinstake] StakeCache: shutdown stats (size=37, hits=125, blocks=5, flushes=0)
```

---

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `-stakecache` | `1` (enabled) | Enable/disable stake cache |
| `-debug=coinstake` | disabled | Enable cache debug logging |

---

## Reorg Protection

When a cache HIT finds a valid kernel, Blackcoin More (like Qtum) re-validates WITHOUT the cache:

```cpp
if (CheckStakeKernelHash(..., stake.blockFromTime, stake.amount, ...)) {
    // Cache hit! But re-validate to protect against deep reorgs
    return CheckKernel(pindexPrev, nBits, nTime, prevout, view);  // No cache!
}
```

This ensures the cached data is still valid after potential chain reorganizations.

---

## Performance Impact

| Scenario | Disk I/O | Chain Traversal |
|----------|----------|-----------------|
| **Without cache** | 1 per UTXO per second | 1 per UTXO per second |
| **With cache** | 1 per UTXO at startup | 1 per UTXO at startup |
| **Winning kernel** | +1 for re-validation | +1 for re-validation |

For 1000 UTXOs checking every second:

- **Without cache:** ~1000 disk reads/sec
- **With cache:** ~0 disk reads/sec (True memory-only path after `g_txindex` removal fix)

---

## Logging

All stake cache events use `BCLog::COINSTAKE` category. Enable with `-debug=coinstake`.

| Event | Description |
|-------|-------------|
| `StakeCache: HIT` | Kernel found via cache |
| `StakeCache: MISS` | Kernel not in cache (disk read) |
| `StakeCache: POPULATE` | Added kernel to cache |
| `StakeCache: FLUSH` | Cache cleared |
| `StakeCache: EVENT` | Surgical removal (UTXO spent) |
| `StakeCache: PERFORMANCE UPDATE` | Efficiency metrics |
| `StakeCache: SHUTDOWN STATS` | Final stats on wallet close |

---

## Files Modified

| File | Changes |
|------|---------|
| `src/pos.h` | `CStakeCache` struct, `CacheKernel()` declaration |
| `src/pos.cpp` | `CheckKernel()` cache overload, `CacheKernel()` implementation |
| `src/wallet/wallet.h` | `stakeCache` member, stats counters |
| `src/wallet/staking.cpp` | Cache population, flush logging, stats |
| `src/wallet/rpc/staking.cpp` | RPC extension |
| `src/wallet/init.cpp` | `-stakecache` argument definition |

---

## References

- **Qtum Source:** [src/wallet/stake.cpp](https://github.com/qtumproject/qtum/blob/master/src/wallet/stake.cpp)
- **Qtum PoS:** [src/pos.cpp](https://github.com/qtumproject/qtum/blob/master/src/pos.cpp)
- **Blackcoin Upgrade Guide:** `UPGRADE.md`
