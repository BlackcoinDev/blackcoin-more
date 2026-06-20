# Staker Timing and Wake/Sleep Design

This document describes the post-refactor staking timing logic in Blackcoin More. The safety bump mechanism was removed; all sleep/timing responsibility now lives in `miner.cpp`.

---

## Overview

Three mechanisms work together to ensure the staker searches each 16-second protocol window exactly once:

1. **`updatedBlockTip()`** — a pure wake signal. It only sets `m_new_block_arrived = true` and notifies `cv_new_block`.
2. **Timer guard** — a per-wallet timestamp barrier (`m_last_coin_stake_search_time`) inside `CreateNewBlock()` that prevents re-searching the same 16-second window.
3. **`MsUntilNextWindow()`** — a boundary-aligned, MTP-aware sleep helper used by `PoSMiner()` for all sleeps.

---

## 1. `updatedBlockTip()` — Pure Wake Signal

**File:** `src/wallet/wallet.cpp`

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

Responsibilities:
- Update `m_best_block_time` for wallet rebroadcast scheduling.
- Ignore notifications during IBD.
- Wake the staker thread via condition variable.

It does **not** compute sleep durations, MTP offsets, or window boundaries.

---

## 2. Wake/Sleep Primitive

**File:** `src/node/miner.cpp:658-686`

```cpp
bool SleepStaker(CWallet *pwallet, uint64_t milliseconds) {
    std::unique_lock<std::mutex> lock(pwallet->cv_block_mutex);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);

    // Check flag BEFORE waiting to prevent race where notification arrives
    // between checking IsStakeClosing() and calling wait_until()
    if (pwallet->m_new_block_arrived.exchange(false)) {
        return true;
    }

    while (std::chrono::steady_clock::now() < deadline) {
        if (pwallet->IsStakeClosing())
            return false;

        auto result = pwallet->cv_new_block.wait_until(lock, deadline);

        if (result == std::cv_status::no_timeout) {
            if (pwallet->m_new_block_arrived.exchange(false)) {
                return true;
            }
        }
    }

    return !pwallet->IsStakeClosing();
}
```

`SleepStaker` sleeps for the requested duration unless:
- A new block arrives (`cv_new_block` notified and flag set)
- The staker is being shut down (`IsStakeClosing()`)

It is the only sleep primitive used by `PoSMiner()`.

---

## 3. `MsUntilNextWindow()` Helper

**File:** `src/node/miner.cpp`

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

The helper:
- Computes the next 16-second boundary from the current adjusted time.
- If that boundary is `<= MTP`, advances by additional 16-second steps until it is strictly after MTP.
- Returns milliseconds until that boundary.

No defensive cap is used. The loop advances exactly one window per iteration. For a pathological sleep, consensus would first have to accept many consecutive max-future-drift blocks, which it does not.

---

## 4. Timer Guard

**File:** `src/node/miner.cpp:258-305`

```cpp
int64_t nSearchTime = txCoinStake.nTime;

if (nSearchTime > pwallet->m_last_coin_stake_search_time) {
    // ... CreateCoinStake() ...
    pwallet->m_last_coin_stake_search_interval = nSearchTime - pwallet->m_last_coin_stake_search_time;
    pwallet->m_last_coin_stake_search_time = nSearchTime;
}
```

`m_last_coin_stake_search_time` defaults to `0`. On the first real search attempt, `nSearchTime > 0` is true, so the guard passes and the timer is initialized naturally. There is no explicit startup initialization.

The timer guard blocks re-searching the same 16-second window even if the staker wakes multiple times within it.

---

## 5. `PoSMiner()` Sleep Paths

**File:** `src/node/miner.cpp:735-906`

After the refactor there are three sleep paths:

| Path | Sleep duration | Purpose |
|---|---|---|
| Failed coinstake (`fPoSCancel`) | `max(MsUntilNextWindow(consensus, pindexPrev->MTP), pos_timio)` | Wait for the next valid window, with `pos_timio` as a CPU-throttling floor |
| After successful block | `MsUntilNextWindow(consensus, newTip->MTP)` | Wait for the first valid window of the new tip |
| Non-PoS fallback (unreachable for staker wallets) | `pos_timio` | Defensive fallback |

`pos_timio` is computed once at startup:

```cpp
pos_timio = gArgs.GetIntArg("-staketimio", DEFAULT_STAKETIMIO)
            + 30 * sqrt(vCoins.size());
```

For large wallets this floor prevents excessive CPU use by bounding how often `CreateNewBlock()` runs.

---

## 6. End-to-End Flow

### New block arrives

```
validation.cpp
  → ValidationSignals::UpdatedBlockTip()
    → NotificationsProxy::UpdatedBlockTip()
      → CWallet::updatedBlockTip()
          → m_new_block_arrived = true
          → cv_new_block.notify_one()
```

### Staker responds

```
PoSMiner loop
  → SleepStaker wakes (flag was set)
  → Clear m_new_block_arrived inside LOCK2
  → CreateNewBlock()
      → txCoinStake.nTime masked to current 16s boundary
      → Timer guard check
      → If new window: CreateCoinStake()
      → If no kernel: fPoSCancel = true
  → If fPoSCancel:
        sleep max(MsUntilNextWindow(...), pos_timio)
  → If success:
        SignBlock + ProcessBlockFound
        sleep MsUntilNextWindow(..., newTip->MTP)
```

The old short-circuit path that skipped `CreateNewBlock()` entirely has been removed. The accepted trade-off is one extra block-template construction per wake, bounded by the timer guard and the `pos_timio` floor.

---

## 7. Historical Note: The Old Safety Bump

The safety bump was a dual-responsibility design where:
- `wallet.cpp` pre-calculated a sleep in `updatedBlockTip()` and stored it in `m_safety_bump_sleep_ms`
- `miner.cpp` read that value in a short-circuit before `CreateNewBlock()`
- `miner.cpp` also had a fallback safety bump inside `CreateNewBlock()`

This was removed because:
- It split timing responsibility across two files.
- It created a race between `m_safety_bump_sleep_ms` and `m_new_block_arrived`.
- The initialization guard for `m_last_coin_stake_search_time` masked the real issue.

The new design computes timing **at stake time** in `miner.cpp` using `MsUntilNextWindow()`.

---

## 8. Key Observations

1. **Single responsibility:** `updatedBlockTip()` only wakes the staker. `PoSMiner()` decides when to sleep.
2. **MTP-aware boundary alignment:** `MsUntilNextWindow()` never sleeps toward a window `<= MTP`.
3. **CPU throttling preserved:** `pos_timio` acts as a floor for the failed-coinstake path.
4. **No jitter:** The post-success rest is deterministic. Stake collision avoidance is handled by the stake modifier, not by timing randomness.
5. **Timer guard still central:** It prevents re-searching the same 16-second window and has not changed semantically since the original Blackcoin code.

---

## 9. Files Involved

| File | Role |
|---|---|
| `src/wallet/wallet.cpp` | `updatedBlockTip()` wake signal |
| `src/wallet/wallet.h` | Wake-on-block fields and timer guard state |
| `src/node/miner.cpp` | `MsUntilNextWindow()`, `CreateNewBlock()`, `PoSMiner()` |
| `src/node/miner.h` | `DEFAULT_STAKETIMIO` |
| `src/pos.cpp` | Protocol timestamp checks (`CheckCoinStakeTimestamp`) |
| `src/validation.cpp` | `FutureDrift()` and `ContextualCheckBlockHeader()` |

---

## 10. No Test Coverage

No test files exercise the staking timing logic. Manual verification on regtest/testnet is recommended after any change to this code.
