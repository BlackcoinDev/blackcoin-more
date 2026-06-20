# Staker Timing Refactor — Final Implementation Diff

**Branch:** v28-CORE  
**Date:** 2026-06-16  
**Status:** Ready for code review. No files have been modified yet.

> ⚠️ This is a read-only plan. All code changes must be reviewed and approved before
> implementation. Compiling, testing, and pushing code are **human tasks**.

---

## Files to Modify

| File | Change Type |
|---|---|
| `src/wallet/wallet.cpp` | Simplify `updatedBlockTip()` |
| `src/wallet/wallet.h` | Remove `m_safety_bump_sleep_ms` field |
| `src/node/miner.cpp` | Remove safety bump, add helper, replace sleeps |
| `src/node/miner.h` | No change (`DEFAULT_STAKETIMIO` kept) |
| `agent/SafetyBump.md` | Full rewrite |
| `agent/staking.md` | Update §11 |
| `agent/staking_probabilities.md` | Update §9 |

---

## 1. `src/wallet/wallet.h`

### Remove field declaration (lines 457–458)

**Remove:**
```cpp
    // Safety bump: pre-calculated sleep time (ms) to next valid 16-second window after new block
    std::atomic<int64_t> m_safety_bump_sleep_ms{0};
```

---

## 2. `src/wallet/wallet.cpp`

### Simplify `updatedBlockTip()` (lines 1585–1622)

**Current:**
```cpp
void CWallet::updatedBlockTip()
{
    m_best_block_time = GetTime();

    if (chain().isInitialBlockDownload()) {
        return;
    }

    if (chain().getTip()) {
        int64_t nNewMTP = chain().getTip()->GetMedianTimePast();
        int64_t nNextWindow = ((nNewMTP + 16 + 15) / 16) * 16;
        int64_t nowAdjusted = GetAdjustedTimeSeconds();
        int64_t sleepMs = (nNextWindow - nowAdjusted) * 1000;

        if (sleepMs > 16000) {
            sleepMs %= 16000;
            if (sleepMs == 0) sleepMs = 16000;
        }

        if (sleepMs > 0) {
            m_safety_bump_sleep_ms = sleepMs;
            LogPrint(BCLog::COINSTAKE, "[%s] UpdatedBlockTip: Pre-calculated next window=%d, sleep=%lld ms "
                      "(MTP=%d, nowAdjusted=%d, diff=%d)\n",
                      GetName(), nNextWindow, sleepMs, nNewMTP, nowAdjusted, nNextWindow - nNewMTP);
        } else {
            m_safety_bump_sleep_ms = 0;
        }
    }

    {
        std::lock_guard<std::mutex> lock(cv_block_mutex);
        m_new_block_arrived.store(true);
    }
    cv_new_block.notify_one();
    LogPrint(BCLog::COINSTAKE, "[%s] WakeOnBlock: staker notified to wake, mtp=%d\n",
             GetName(),
             chain().getTip() ? chain().getTip()->GetMedianTimePast() : 0);
}
```

**New:**
```cpp
void CWallet::updatedBlockTip()
{
    m_best_block_time = GetTime();

    if (chain().isInitialBlockDownload()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(cv_block_mutex);
        m_new_block_arrived.store(true);
    }
    cv_new_block.notify_one();
}
```

**Rationale:** The wallet should only wake the staker. All boundary and MTP math moves
entirely to `miner.cpp`.

---

## 3. `src/node/miner.cpp`

### 3a. Add `MsUntilNextWindow()` helper (near `UpdateTime()`, lines ~50–80)

**Add:**
```cpp
int64_t MsUntilNextWindow(const Consensus::Params& consensus, int64_t mtp)
{
    int64_t now = GetAdjustedTimeSeconds();
    int64_t nextBoundary = (now & ~consensus.nStakeTimestampMask)
                         + (consensus.nStakeTimestampMask + 1);

    // Advance past MTP. Block timestamp must be strictly greater than MTP.
    while (nextBoundary <= mtp)
        nextBoundary += (consensus.nStakeTimestampMask + 1);

    return std::max(0LL, (nextBoundary - now) * 1000);
}
```

**No defensive cap.** The loop advances exactly one window per iteration. For MTP to
produce a pathological sleep, consensus would first have to accept a chain of 11+
consecutive max-future-drift blocks, which it does not.

---

### 3b. Remove safety bump and initialization guard from `CreateNewBlock()` (lines ~219–254)

**Remove entire block:**
```cpp
        // Safety Bump: if current timestamp slot is blocked by MTP, advance to next
        // valid 16-second window and calculate sleep time. This avoids tight-loop
        // fPoSCancel failures where nTime <= MTP.
        int64_t nSafetyBumpSleepMs = 0;
        if (txCoinStake.nTime <= pindexPrev->GetMedianTimePast()) {
            uint32_t oldTime = txCoinStake.nTime;
            while (txCoinStake.nTime <= pindexPrev->GetMedianTimePast()) {
                txCoinStake.nTime += (chainparams.GetConsensus().nStakeTimestampMask + 1);
            }

            int64_t now = GetAdjustedTimeSeconds();
            int64_t timeUntilWindow = (txCoinStake.nTime - now) * 1000;
            if (timeUntilWindow > 0) {
                nSafetyBumpSleepMs = timeUntilWindow;

                if (nSafetyBumpSleepMs > 16000) {
                    LogPrint(BCLog::COINSTAKE, "[%s] Minter: Stripping MTP inflation from sleep: %lld ms -> %lld ms\n",
                             pwallet->GetName(), nSafetyBumpSleepMs, nSafetyBumpSleepMs % 16000);
                    nSafetyBumpSleepMs %= 16000;
                    if (nSafetyBumpSleepMs == 0) nSafetyBumpSleepMs = 16000;
                }
            }

            LogPrint(BCLog::COINSTAKE, "[%s] Minter: Safety Bump fallback triggered! Skipped window %d, starting search at %d (Next Window), sleeping %lld ms\n",
                     pwallet->GetName(), oldTime, txCoinStake.nTime, nSafetyBumpSleepMs);
        }

        // Per-wallet staking timer for multi-wallet independence
        if (pwallet->m_last_coin_stake_search_time == 0) {
            pwallet->m_last_coin_stake_search_time = GetAdjustedTimeSeconds();
            LogPrint(BCLog::COINSTAKE, "[%s] Wallet timer initialized: last_search_time=%d\n",
                     pwallet->GetName(), pwallet->m_last_coin_stake_search_time);
        }
```

**What survives:** `nSearchTime` assignment and `nSearchTime > m_last_coin_stake_search_time`
guard immediately follow and are **kept unchanged**. `m_last_coin_stake_search_time` is
still updated at line ~305. The timer guard alone is sufficient to prevent re-searching
the same 16-second window.

---

### 3c. Remove `m_safety_bump_sleep_ms` write-back on `fPoSCancel` (lines ~308–312)

**Current:**
```cpp
        if (*pfPoSCancel) {
            // Pass safety bump sleep to caller so PoSMiner can short-circuit
            if (nSafetyBumpSleepMs > 0) {
                pwallet->m_safety_bump_sleep_ms = nSafetyBumpSleepMs;
            }
            return nullptr;
        }
```

**New:**
```cpp
        if (*pfPoSCancel) {
            return nullptr; // peercoin: there is no point to continue if we failed to create coinstake
        }
```

---

### 3d. Update ghost-block comment (lines ~278–280)

**Current:**
```cpp
                    // Ghost block: valid kernel found but timestamp <= MTP.
                    // With safety bump this should NEVER trigger. If it does, it
                    // indicates a bug (race condition or timing issue).
```

**New:**
```cpp
                    // Ghost block: valid kernel found but timestamp <= MTP.
                    // The timer guard and boundary-aligned sleeps in PoSMiner()
                    // prevent this under normal operation. If this fires, it
                    // indicates a bug in MsUntilNextWindow() or the timer guard logic.
```

---

### 3e. Remove short-circuit consumer from `PoSMiner()` (lines ~804–815)

**Remove entire block:**
```cpp
            // Safety bump short-circuit: if updatedBlockTip() pre-calculated a sleep,
            // consume it directly without entering CreateNewBlock(). This avoids
            // unnecessary LOCK2(cs_wallet, cs_main) and wasted block template
            // construction when we know the current timestamp <= MTP.
            {
                int64_t safetyBump = pwallet->m_safety_bump_sleep_ms.exchange(0);
                if (safetyBump > 0) {
                    LogPrint(BCLog::COINSTAKE, "[%s] Minter: Short-circuit safety bump sleep=%lld ms (skipping block assembly)\n",
                             pwallet->GetName(), safetyBump);
                    if (!SleepStaker(pwallet, safetyBump))
                        return;
                    LogPrint(BCLog::COINSTAKE, "[%s] Minter: Woke up from short-circuit safety bump, resuming staking search\n",
                             pwallet->GetName());
                    continue;
                }
            }
```

**Trade-off acknowledged:** Without this short-circuit, every new block wake will
proceed to `CreateNewBlock()` with `LOCK2`. This is slightly more CPU work per block,
but correctness is preserved because the timer guard blocks re-searching the same window.

---

### 3f. Replace `fPoSCancel` fallback sleep (lines ~856–867)

**Current:**
```cpp
                if (fPoSCancel == true)
                {
                    // Use safety bump sleep if available, otherwise fall back to pos_timio
                    int64_t safetyBumpSleep = pwallet->m_safety_bump_sleep_ms.exchange(0);
                    int64_t sleepTime = safetyBumpSleep > 0 ? safetyBumpSleep : pos_timio;

                    if (!SleepStaker(pwallet, sleepTime))
                        return;

                    if (safetyBumpSleep > 0) {
                        LogPrint(BCLog::COINSTAKE, "[%s] Minter: Woke up from safety bump sleep, resuming staking search\n",
                                 pwallet->GetName());
                    }
                    continue;
                }
```

**New:**
```cpp
                if (fPoSCancel == true)
                {
                    int64_t sleepMs = std::max<int64_t>(
                        MsUntilNextWindow(Params().GetConsensus(), pindexPrev->GetMedianTimePast()),
                        static_cast<int64_t>(pos_timio));

                    LogPrint(BCLog::COINSTAKE, "[%s] Minter: No coinstake yet, sleeping %lld ms until next window\n",
                             pwallet->GetName(), sleepMs);

                    if (!SleepStaker(pwallet, sleepMs))
                        return;
                    continue;
                }
```

> ⚠️ **Cast required:** `pos_timio` is `unsigned int`. Without `static_cast<int64_t>`,
> `std::max<int64_t>` compares signed vs unsigned, which compiles but may produce
> compiler warnings. The cast is mandatory for a clean build.

**`pos_timio` acts as the floor** for CPU throttling on large wallets
(`DEFAULT_STAKETIMIO + 30 * sqrt(vCoins.size())`).

---

### 3g. Replace post-success rest (lines ~891–894)

**Current:**
```cpp
                ProcessBlockFound(pblock, pwallet->chain().chainman());
                // Rest for ~16 seconds after successful block to preserve close quick
                uint64_t stakerRestTime = (16 + FastRandomContext().randrange(4)) * 1000;
                if (!SleepStaker(pwallet, stakerRestTime))
                    return;
                continue;
```

**New:**
```cpp
                ProcessBlockFound(pblock, pwallet->chain().chainman());
                // Sleep until the first valid stake window of the new tip.
                int64_t sleepMs = MsUntilNextWindow(Params().GetConsensus(),
                                                     pwallet->chain().getTip()->GetMedianTimePast());
                if (!SleepStaker(pwallet, sleepMs))
                    return;
                continue;
```

**No jitter.** Stake collision avoidance is handled at the kernel level (stake modifier),
not by timing randomness. Deterministic boundary-aligned sleep is strictly better with
the wake-on-block design.

---

### 3h. Keep non-PoS fallback unchanged

```cpp
            if (!SleepStaker(pwallet, pos_timio))
                return;
```

This path is unreachable for staker wallets but retained for defensive programming.

---

## 4. `src/node/miner.h`

**No changes.** `DEFAULT_STAKETIMIO` is kept because `pos_timio` remains as a sleep floor.

---

## 5. Documentation Updates

### `agent/SafetyBump.md` — Full rewrite

Replace with a description of the new single-responsibility design:
- No `m_safety_bump_sleep_ms`
- `updatedBlockTip()` is only a wake signal
- `PoSMiner()` computes boundary-aligned sleeps via `MsUntilNextWindow()`
- Timer guard prevents re-searching the same window
- `pos_timio` acts as a CPU-throttling floor

### `agent/staking.md` §11

- Remove references to `m_safety_bump_sleep_ms`
- Describe `MsUntilNextWindow()` and its MTP-awareness
- Explain why `updatedBlockTip()` is now a pure wake signal
- Keep the timer guard explanation
- Update Key Observations bullet 6

### `agent/staking_probabilities.md` §9

- Remove or rewrite the safety bump bullet
- Add a note that all sleeps are boundary-aligned and MTP-aware via `MsUntilNextWindow()`

---

## 6. Testing Plan

> 👤 **Building, running tests, and verifying on a live node are human tasks.**

1. **Build** the project and confirm no compile errors or new warnings.
2. **Unit tests:** `make check` or equivalent.
3. **Regtest staker:**
   - Start with `blackmored -regtest`
   - Enable staking
   - Produce blocks via `generateblock` or `generatetoaddress`
   - Verify `PoSMiner` wakes and sleeps at 16-second boundaries
4. **Testnet/mainnet staker (recommended):**
   - Verify `getstakinginfo` reports correct search-interval
   - Verify no increase in orphan rate over several hours
5. **Check `debug.log` for:**
   - No `"Short-circuit safety bump"` messages
   - No `"Safety Bump fallback triggered"` messages
   - `"Minter: No coinstake yet, sleeping ... ms until next window"` messages at expected intervals

---

## 7. Risk Summary

| Risk | Mitigation |
|---|---|
| Staker attempts search at `nTime <= MTP` | `MsUntilNextWindow()` advances past MTP |
| Large wallets consume too much CPU | `pos_timio` floor preserved |
| Time jumps backward | `std::max(0LL, ...)` prevents negative sleep |
| Long sleeps under weird mock time | No cap — protocol / chain validation keeps MTP sane |
| `m_safety_bump_sleep_ms` dangling references | Verified: only 3 source files used it |
| Extra `LOCK2` per block wake (no short-circuit) | Timer guard prevents redundant work; accepted trade-off |
| Signed/unsigned mismatch in `std::max` | `static_cast<int64_t>(pos_timio)` applied in location 3f |

---

## Human Responsibilities

> 👤 **The following tasks must be performed by a human developer and must not be delegated
> to an AI agent.**

- **Applying the code changes** — editing the source files according to this diff plan
- **Compiling the software** — building the codebase and resolving any errors or warnings
- **Running tests locally** — executing the test suite before opening a pull request
- **Pushing code** — staging, committing, and pushing changes to the branch
- **Opening and merging pull requests** — requesting reviews, addressing feedback, merging
- **Live node verification** — running a testnet/regtest node and observing staker behavior
