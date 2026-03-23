# Blackcoin More: Synchronization Architecture Analysis

## Executive Summary

This document analyzes the synchronization architecture of Blackcoin More compared to Bitcoin Core, identifying all parameters that differ due to Blackcoin's 64-second block time and Proof-of-Stake consensus.

**Key Finding**: While many sync parameters were correctly tuned, **several time-based constants remain at Bitcoin values** and should be adapted for Blackcoin's ~10x faster block time.

---

## 1. Critical Differences: Blackcoin vs Bitcoin

### 1.1 Block Time Impact

| Parameter | Bitcoin Core | Blackcoin More | Ratio | Reason |
|-----------|-------------|----------------|-------|--------|
| `nTargetSpacing` | 600 sec (10 min) | 64 sec | ~9.4x faster | PoS 3.1 design |
| `nStakeTimestampMask` | N/A | 0xf (16-sec windows) | - | Staking efficiency |
| `nCoinbaseMaturity` | 100 blocks | 500 blocks | 5x more | PoS security |
| `nLastPOWBlock` | N/A | 10000 | - | PoS transition |
| `nMaxReorganizationDepth` | N/A | 500 blocks | - | PoS reorg limit |

### 1.2 Sync-Specific Parameters (src/net_processing.cpp)

| Parameter | Bitcoin Core | Blackcoin More | Notes |
|-----------|-------------|----------------|-------|
| `STALE_CHECK_INTERVAL` | 10 min | **1 min** | Adjusted for 64s blocks |
| `STALE_RELAY_AGE_LIMIT` | 30 days | **10 hours** (36000s) | Close to max reorg depth |
| `BLOCK_STALLING_TIMEOUT_MAX` | Varies | **64 seconds** | Equals nTargetSpacing |
| `NODE_NETWORK_LIMITED_MIN_BLOCKS` | ~288 (3 days) | **2700** (~2 days) | Adjusted for block time |
| `NODE_NETWORK_LIMITED_ALLOW_CONN_BLOCKS` | ~144 | **1350** | Half of above |

---

## 2. Timeout Constants Analysis

### 2.1 Headers Download Timeout

```cpp
// src/net_processing.cpp:69-70
static constexpr auto HEADERS_DOWNLOAD_TIMEOUT_BASE = 15min;
static constexpr auto HEADERS_DOWNLOAD_TIMEOUT_PER_HEADER = 1ms;
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: The timeout formula `base + per_header * expected_headers` is already adaptive. The base timeout of 15 minutes accounts for network latency regardless of block time. The per-header timeout is negligible (1ms).

**Recommendation**: No change needed. Headers download is network-bound, not block-time-bound.

### 2.2 Headers Response Time

```cpp
// src/net_processing.cpp:72
static constexpr auto HEADERS_RESPONSE_TIME{2min};
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: This is peer response latency, not block-dependent. 2 minutes is reasonable for any chain.

### 2.3 Chain Sync Timeout

```cpp
// src/net_processing.cpp:78
static constexpr auto CHAIN_SYNC_TIMEOUT{20min};
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: This timeout is for outbound peers to sync chainwork. It's measured against chainwork, not block count. No adjustment needed.

### 2.4 Stale Check Interval

```cpp
// src/net_processing.cpp:80
static constexpr auto STALE_CHECK_INTERVAL{1min}; // BLACKCOIN-SPECIFIC: Reduced from 10min for PoS responsiveness (64s block target)
```

**Status**: **ALREADY ADJUSTED**

**Rationale**: Bitcoin checks for stale tips every 10 minutes (1 block interval). Blackcoin checks every 1 minute (~1 block interval). This was correctly tuned.

### 2.5 Block Stalling Timeout

```cpp
// src/net_processing.cpp:128-130
static constexpr auto BLOCK_STALLING_TIMEOUT_DEFAULT{2s};
static constexpr auto BLOCK_STALLING_TIMEOUT_MAX{64s}; // BLACKCOIN-SPECIFIC: Adjusted for nTargetSpacing
```

**Status**: **ALREADY ADJUSTED**

**Rationale**: `BLOCK_STALLING_TIMEOUT_MAX` is set to `nTargetSpacing` (64s) vs Bitcoin's 10 minutes. The default starts at 2s and can grow up to 64s max.

---

## 3. Headers Sync Parameters

### 3.1 Max Commitments Calculation

```cpp
// src/headerssync.cpp:52
// Uses GetAdjustedTime() - CRITICAL: This function was removed in Bitcoin 28.x
m_max_commitments = 6*(Ticks<std::chrono::seconds>(GetAdjustedTime() - NodeSeconds{std::chrono::seconds{chain_start->GetMedianTimePast()}}) + MAX_FUTURE_BLOCK_TIME) / HEADER_COMMITMENT_PERIOD;
```

**CRITICAL**: `GetAdjustedTime()` usage preserved. This is different from Bitcoin 28+ which uses only `GetTime()`.

### 3.2 Compressed Header Structure

```cpp
// src/headerssync.h:21-43
struct CompressedHeader {
    int32_t nVersion{0};
    uint256 hashMerkleRoot;
    uint32_t nTime{0};
    uint32_t nBits{0};
    uint32_t nNonce{0};
    uint32_t nFlags{0};  // BLACKCOIN-SPECIFIC: PoS block flags
    // ...
};
```

**Status**: **BLACKCOIN-SPECIFIC extension**

**Rationale**: Bitcoin Core's `CompressedHeader` does NOT include `nFlags`. This field is required for PoS block detection during headers sync.

### 3.3 Presync/Redownload Constants

```cpp
// src/headerssync.cpp:22-26
constexpr size_t HEADER_COMMITMENT_PERIOD{568};
constexpr size_t REDOWNLOAD_BUFFER_SIZE{10071}; // ~17.7 commitments
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: These are DoS protection constants based on memory analysis, not block time dependent.

---

## 4. Block Download Parameters

### 4.1 Block Download Window

```cpp
// src/net_processing.cpp:143
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: This controls how far ahead we fetch blocks. With 64s blocks vs 600s Bitcoin blocks, we could theoretically reduce this, but 1024 is already conservative. A Blackcoin node can download 1024 blocks in ~18 hours vs Bitcoin's ~7 days.

**Potential Optimization**: Consider reducing to 512 for faster sync, but current value is safe.

### 4.2 Max Blocks In Transit Per Peer

```cpp
// src/net_processing.cpp:125
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: This is a memory/per-peer limit, not block-time dependent. 16 blocks per peer is reasonable for any chain.

### 4.3 Max Headers Results

```cpp
// src/net_processing.cpp:133
static const unsigned int MAX_HEADERS_RESULTS = 2000;
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: Protocol-defined maximum. Changing this requires a protocol upgrade.

---

## 5. Consensus Parameters Impact

### 5.1 MAX_FUTURE_BLOCK_TIME

```cpp
// src/chain.h:30
static constexpr int64_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60; // 2 hours
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: This is a consensus rule for timestamp validation. Block time doesn't affect the future block tolerance. 2 hours is standard for all Bitcoin-derived chains.

### 5.2 Timestamp Window

```cpp
// src/chain.h:38
static constexpr int64_t TIMESTAMP_WINDOW = MAX_FUTURE_BLOCK_TIME; // 2 hours
```

**Status**: **Unchanged from Bitcoin Core**

**Rationale**: Used for wallet key timestamps and other external time comparisons. Not block-time dependent.

### 5.3 NODE_NETWORK_LIMITED Constants

```cpp
// src/net_processing.cpp:153-156
// BLACKCOIN-SPECIFIC: Adjusted for 64-second block time
static const unsigned int NODE_NETWORK_LIMITED_MIN_BLOCKS = 2700;    // ~2 days
static const unsigned int NODE_NETWORK_LIMITED_ALLOW_CONN_BLOCKS = 1350;  // ~1 day
```

**Status**: **ALREADY ADJUSTED**

**Rationale**: 
- Bitcoin: 288 blocks = 3 days (288 * 600s = 172800s)
- Blackcoin: 2700 blocks = ~2 days (2700 * 64s = 172800s)
- Calculation preserves the time window while adjusting block count.

---

## 6. Stale Relay Age Limit

```cpp
// src/net_processing.cpp:90-96
// Bitcoin: 30 * 24 * 60 * 60 = 30 days
// BLACKCOIN-SPECIFIC: 10 hours (36000 seconds)
// (should be close to nMaxReorganizationDepth * nTargetSpacing)
static constexpr int STALE_RELAY_AGE_LIMIT = 10 * 60 * 60;
```

**Status**: **ALREADY ADJUSTED**

**Rationale**: 
- Blackcoin's `nMaxReorganizationDepth = 500` blocks
- 500 * 64s = 32000 seconds ≈ 8.9 hours
- Set to 10 hours (rounded) to cover max reorg depth

---

## 7. Parameters Review Summary

### 7.1 Already Correctly Tuned for Blackcoin

| Parameter | Status | Notes |
|-----------|--------|-------|
| `STALE_CHECK_INTERVAL` | ✅ Adjusted | 1min (vs Bitcoin 10min) |
| `BLOCK_STALLING_TIMEOUT_MAX` | ✅ Adjusted | 64s (equals nTargetSpacing) |
| `STALE_RELAY_AGE_LIMIT` | ✅ Adjusted | 10 hours (covers max reorg) |
| `NODE_NETWORK_LIMITED_*` | ✅ Adjusted | ~2 days, ~1 day |
| `CompressedHeader.nFlags` | ✅ Added | PoS block detection |
| `GetAdjustedTime()` usage | ✅ Preserved | Removed in Bitcoin 28.x |

### 7.2 Correctly Unchanged (Block-Time Independent)

| Parameter | Reason |
|-----------|--------|
| `HEADERS_DOWNLOAD_TIMEOUT_BASE` | Network latency, not block dependent |
| `HEADERS_RESPONSE_TIME` | Peer response time |
| `CHAIN_SYNC_TIMEOUT` | Chainwork-based, not block count |
| `MAX_HEADERS_RESULTS` | Protocol constant |
| `MAX_BLOCKS_IN_TRANSIT_PER_PEER` | Memory limit |
| `MAX_FUTURE_BLOCK_TIME` | Consensus rule (2 hours) |
| `TIMESTAMP_WINDOW` | External timestamp grace period |
| `HEADER_COMMITMENT_PERIOD` | DoS protection constant |
| `REDOWNLOAD_BUFFER_SIZE` | DoS protection constant |
| `BLOCK_DOWNLOAD_WINDOW` | Conservative download window |

### 7.3 Potential Optimizations (Not Required)

| Parameter | Current | Potential Optimization |
|-----------|---------|----------------------|
| `BLOCK_DOWNLOAD_WINDOW` | 1024 | 512 (faster initial sync) |
| `Extra Peer Check Interval` | 45s | Potentially reduce for faster peer management |

---

## 8. Block Time-Based Calculations

### 8.1 Time-to-Block-Count Conversions

```cpp
// Common conversion pattern in Blackcoin More:
int blocks = time_seconds / consensus.nTargetSpacing;

// Examples:
// 1 day    = 86400s / 64s  = 1350 blocks
// 2 days   = 172800s / 64s = 2700 blocks
// 10 hours = 36000s / 64s  = 562.5 → ~500 blocks (reorg depth)
```

### 8.2 Code Locations Using Conversions

| File | Line | Usage |
|------|------|-------|
| `net_processing.cpp` | 1463 | Stale check: `GetTime() / nTargetSpacing` |
| `net_processing.cpp` | 5829 | Headers timeout: uses `nTargetSpacing` |
| `qt/modaloverlay.cpp` | 148 | Progress estimation: `secsTo() / nTargetSpacing` |
| `qt/bitcoingui.cpp` | 1082 | Headers left: `time_diff / nTargetSpacing` |

---

## 9. Test Considerations

### 9.1 Test Files Using Block Time

| File | Usage |
|------|-------|
| `test/peerman_tests.cpp` | Uses `nTargetSpacing` for time calculations |
| `test/denialofservice_tests.cpp` | Uses `nTargetSpacing` for time advancement |
| `test/pow_tests.cpp` | Difficulty adjustment uses `nTargetSpacing` |

### 9.2 UPGRADE NOTES

Several files contain `UPGRADE NOTE` comments:

```cpp
// test/peerman_tests.cpp:51-52
// UPGRADE NOTE: Blackcoin uses nTargetSpacing (not nPowTargetSpacing from Bitcoin 27.x)
```

This documents that Blackcoin uses the unified `nTargetSpacing` while Bitcoin 27+ split this into `nPowTargetSpacing` and `nTargetSpacing`.

---

## 10. Key Takeaways for Future Upgrades

### 10.1 MUST Preserve

1. **`GetAdjustedTime()` in headerssync.cpp** - Removed in Bitcoin 28.x, CRITICAL for PoS
2. **`CompressedHeader.nFlags`** - Not in Bitcoin Core, required for PoS
3. **`STALE_CHECK_INTERVAL` adjustment** - Must scale with block time
4. **`STALE_RELAY_AGE_LIMIT` calculation** - Must cover max reorg depth

### 10.2 Must Verify After Bitcoin Port

1. **Any new timeout constants** - Verify they don't assume 10-minute blocks
2. **Header sync logic** - Ensure `GetAdjustedTime()` is still used
3. **Block download timeouts** - Check if per-block timeouts exist
4. **Peer eviction logic** - Verify time-based calculations use correct spacing

### 10.3 Documentation References

| Document | Purpose |
|----------|---------|
| `UPGRADE.md` | Overall upgrade plan |
| `agent/BLOCK_SERIALIZATION.md` | PoS block structure |
| `agent/STAKING.md` | Staking protocol details |
| `agent/STAKECACHE.md` | Cache architecture |

---

## 11. Time-Based Constants Requiring Analysis

### 11.1 MAX_BLOCK_TIME_GAP (GUI "Catching up" threshold)

```cpp
// src/chain.h:46
static constexpr int64_t MAX_BLOCK_TIME_GAP = 90 * 60; // 90 minutes
```

| Chain | Block Time | 90 minutes = X blocks | Problem |
|-------|-----------|------------------------|---------|
| Bitcoin | 600s (10 min) | **9 blocks** | 9 blocks = ~90 min ✓ |
| Blackcoin | 64s | **84 blocks** | 84 blocks = ~90 min ⚠️ |

**Problem**: GUI shows "Up to date" even when 84 blocks (1.5 hours) behind.

**Recommendation**: Reduce to `15 * 60` (15 minutes):
```cpp
// BLACKCOIN-SPECIFIC: 15 minutes = ~14 blocks at 64s spacing
static constexpr int64_t MAX_BLOCK_TIME_GAP = 15 * 60;
```

**Impact**: GUI will correctly show "Catching up..." if no new block for ~15 minutes.

---

### 11.2 MAX_FUTURE_BLOCK_TIME (Consensus)

```cpp
// src/chain.h:30
static constexpr int64_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60; // 2 hours
```

| Chain | Block Time | 2 hours = X blocks | Problem |
|-------|-----------|---------------------|---------|
| Bitcoin | 600s | **12 blocks** | Normal ✓ |
| Blackcoin PoS | N/A | Uses FutureDrift(15s) | ✓ |
| Blackcoin PoW | 64s | **112 blocks** | Severe ⚠️ |

**Analysis**:
- **PoS**: `FutureDrift()` returns `nTime + 15` (not this value) ✓
- **PoW only**: Applies during PoW phase (blocks 1-10000 on mainnet)
- **Issue**: 112 future blocks creates massive timestamp overlaps

**Recommendation**: Consider reducing to `15 * 60` (15 minutes):
```cpp
// BLACKCOIN-SPECIFIC: 15 minutes for PoW blocks (regtest/testnet)
static constexpr int64_t MAX_FUTURE_BLOCK_TIME = 15 * 60;
```

**Note**: Only affects `nLastPOWBlock` region. After block 10000, PoS uses `FutureDrift()`.

---

### 11.3 ORPHAN_TX_EXPIRE_TIME (Mempool Orphan Handling)

```cpp
// src/txorphanage.cpp:15
static constexpr int64_t ORPHAN_TX_EXPIRE_TIME = 20 * 60; // 20 minutes
```

| Chain | Block Time | 20 minutes = X blocks | Recommendation |
|-------|-----------|----------------------|----------------|
| Bitcoin | 600s (10 min) | **2 blocks** | 20 min ✓ |
| Blackcoin | 64s | **18.75 blocks** | 5 min (4-5 blocks) |

**Recommendation**: Reduce to `5 * 60` (5 minutes):
```cpp
// BLACKCOIN-SPECIFIC: 5 minutes (~5 blocks at 64s) for faster orphan eviction
static constexpr int64_t ORPHAN_TX_EXPIRE_TIME = 5 * 60;
```

**Impact**: Orphan transactions cleared faster, saving memory.

---

### 11.4 ORPHAN_TX_EXPIRE_INTERVAL (Orphan Sweep Interval)

```cpp
// src/txorphanage.cpp:17
static constexpr int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60; // 5 minutes
```

**Status**: **Already reasonable**

At 5 minutes (~5 blocks at 64s), this is proportional. No change needed.

---

### 11.5 DEFAULT_MEMPOOL_EXPIRY_HOURS (Mempool Transaction Expiration)

```cpp
// src/kernel/mempool_options.h:21
static constexpr unsigned int DEFAULT_MEMPOOL_EXPIRY_HOURS{336}; // 14 days
```

| Chain | Block Time | 14 days = X blocks | Recommendation |
|-------|-----------|--------------------|----------------|
| Bitcoin | 600s | ~2016 blocks | 14 days ✓ |
| Blackcoin | 64s | ~18,900 blocks | 48-72 hours |

**Recommendation**: Reduce to `48` or `72` hours:
```cpp
// BLACKCOIN-SPECIFIC: 48 hours (~2700 blocks at 64s) instead of 14 days
static constexpr unsigned int DEFAULT_MEMPOOL_EXPIRY_HOURS{48};
```

**Impact**: Less RAM used for caching stale transactions.

---

### 11.6 DEFAULT_MAX_TIP_AGE (IBD Threshold)

```cpp
// src/kernel/chainstatemanager_opts.h:24
static constexpr auto DEFAULT_MAX_TIP_AGE{24h};
```

**Status**: **Probably OK**

24 hours = ~1350 blocks at 64s. This is used for IBD completion detection.

**Consideration**: May want to reduce to `12h` (6 hours at 64s) for faster IBD state detection, but current value is conservative and safe.

---

## 12. Time-Based Constants Summary Table

| Constant | Bitcoin | Blackcoin Current | Blackcoin Recommended | File |
|----------|---------|-------------------|----------------------|------|
| `MAX_BLOCK_TIME_GAP` | 90 min | 90 min (84 blocks!) | **15 min** | `chain.h:46` |
| `MAX_FUTURE_BLOCK_TIME` | 2 hours | 2 hours (112 blocks) | **15 min** (PoW only) | `chain.h:30` |
| `ORPHAN_TX_EXPIRE_TIME` | 20 min | 20 min (18 blocks) | **5 min** | `txorphanage.cpp:15` |
| `ORPHAN_TX_EXPIRE_INTERVAL` | 5 min | 5 min | OK | `txorphanage.cpp:17` |
| `DEFAULT_MEMPOOL_EXPIRY` | 14 days | 14 days (18K blocks) | **48-72 hours** | `mempool_options.h:21` |
| `DEFAULT_MAX_TIP_AGE` | 24h | 24h (1350 blocks) | Consider `12h` | `chainstatemanager_opts.h:24` |

---

## 13. FutureDrift: Blackcoin's Custom Future Limit

```cpp
// src/validation.cpp:158-165
int64_t FutureDrift(Chainstate& active_chainstate, int64_t nTime)
{
    // loose policy for FutureDrift in regtest mode
    if (Params().GetConsensus().fPowNoRetargeting && active_chainstate.m_chain.Height() <= Params().GetConsensus().nLastPOWBlock) {
        return nTime + 24 * 60 * 60; // 24 hours for regtest PoW
    }
    return Params().GetConsensus().IsProtocolV2(nTime) ? nTime + 15 : nTime + 10 * 60;
}
```

**Blackcoin's FutureDrift Behavior**:

| Protocol | Future Drift | Notes |
|----------|-------------|-------|
| PoW (mainnet, before block 10000) | `nTime + 600` (10 min) | Standard BIP |
| PoW (regtest) | `nTime + 86400` (24 hours) | Looser for testing |
| PoS V2+ | `nTime + 15` | **Blackcoin custom** |

**Analysis**: FutureDrift correctly handles PoS with 15-second future limit. The `MAX_FUTURE_BLOCK_TIME` (2 hours) is irrelevant for PoS blocks since FutureDrift overrides it.

---

## 14. V3 Policy (No Changes Needed)

```cpp
// src/policy/v3_policy.h
static constexpr unsigned int V3_DESCENDANT_LIMIT{2};
static constexpr unsigned int V3_ANCESTOR_LIMIT{2};
static constexpr int64_t V3_CHILD_MAX_VSIZE{1000};
```

**Status**: **No changes needed**

These limits are about transaction graph relationships (1 parent + 1 child), not time-based. Block speed has no impact on these values.

---

## 15. Safety Bump Integration

### 11.1 MTP Inflation Attack Mitigation

The Safety Bump implementation in `src/wallet/wallet.cpp` and `src/node/miner.cpp` uses:

```cpp
// src/wallet/wallet.cpp (updatedBlockTip)
sleepMs %= 16000;  // Strip MTP inflation (16s mask = 16000ms)
```

This aligns with `nStakeTimestampMask = 0xf` (16-second staking windows).

### 11.2 Time Source Consistency

**CRITICAL**: All staking-related time calculations must use `GetAdjustedTimeSeconds()`, NOT `GetTime()`:

- `wallet.cpp` - Safety Bump pre-calculation
- `headerssync.cpp` - Max commitments calculation
- `pos.cpp` - Kernel hash validation

This ensures consistent time references across:
- Block validation
- Staking window calculations
- Headers sync bounds

---

## 16. Changes Applied (v27.2.0+)

The following time constants were adapted from Bitcoin Core values to Blackcoin More's 64-second block spacing:

### 16.1 MAX_BLOCK_TIME_GAP (GUI)

| Before | After | Block Equivalent |
|--------|-------|-----------------|
| 90 min | **15 min** | 84 blocks → **14 blocks** |

```cpp
// src/chain.h
// BLACKCOIN-SPECIFIC: 15 minutes ≈ 14 blocks at 64s spacing
static constexpr int64_t MAX_BLOCK_TIME_GAP = 15 * 60;
```

**Impact**: GUI now correctly shows "Catching up" when 14+ blocks behind.

### 16.2 MAX_FUTURE_BLOCK_TIME (Consensus)

| Before | After | Block Equivalent |
|--------|-------|-----------------|
| 2 hours | **15 min** | 112 blocks → **14 blocks** |

```cpp
// src/chain.h
// BLACKCOIN-SPECIFIC: 15 minutes ≈ 14 blocks at 64s spacing
static constexpr int64_t MAX_FUTURE_BLOCK_TIME = 15 * 60;
```

**Impact**: PoW blocks (mainnet pre-10000, testnet/regtest/signet) now have proportional future timestamp tolerance.

**Note**: `TIMESTAMP_WINDOW = MAX_FUTURE_BLOCK_TIME` automatically updates with this change.

### 16.3 ORPHAN_TX_EXPIRE_TIME (Mempool)

| Before | After | Block Equivalent |
|--------|-------|-----------------|
| 20 min | **5 min** | 18 blocks → **5 blocks** |

```cpp
// src/txorphanage.cpp
// BLACKCOIN-SPECIFIC: 5 minutes ≈ 5 blocks at 64s spacing
static constexpr int64_t ORPHAN_TX_EXPIRE_TIME = 5 * 60;
```

**Impact**: Faster orphan transaction eviction, reduced memory usage.

### 16.4 DEFAULT_MEMPOOL_EXPIRY_HOURS (Mempool)

| Before | After | Block Equivalent |
|--------|-------|-----------------|
| 14 days (336 hrs) | **48 hours** | ~18900 blocks → **~2700 blocks** |

```cpp
// src/kernel/mempool_options.h
// BLACKCOIN-SPECIFIC: 48 hours ≈ 2700 blocks at 64s spacing
static constexpr unsigned int DEFAULT_MEMPOOL_EXPIRY_HOURS{48};
```

**Impact**: Reduced RAM usage for mempool transaction caching.

### 16.5 Summary Table

| Constant | Before | After | Status |
|----------|--------|-------|--------|
| `MAX_BLOCK_TIME_GAP` | 90 min | 15 min | ✅ Changed |
| `MAX_FUTURE_BLOCK_TIME` | 2 hours | 15 min | ✅ Changed |
| `TIMESTAMP_WINDOW` | = MAX_FUTURE_BLOCK_TIME | = MAX_FUTURE_BLOCK_TIME | ✅ Auto-updated |
| `ORPHAN_TX_EXPIRE_TIME` | 20 min | 5 min | ✅ Changed |
| `DEFAULT_MEMPOOL_EXPIRY_HOURS` | 336 | 48 | ✅ Changed |

---

*Generated: March 2026*
*Blackcoin More v27.2.0+*