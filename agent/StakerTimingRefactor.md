# Staker Timing Refactor — Finalized Plan

**Branch:** v28-CORE  
**Date:** 2026-06-16  
**Status:** Approved for implementation (pending code review)

---

## Background

The current staking timing logic has a dual-responsibility design problem: timing is split across
`wallet.cpp` (`updatedBlockTip`) and `miner.cpp` (`PoSMiner` / `CreateNewBlock`). The wallet
pre-calculates a sleep duration based on MTP and passes it to the miner via `m_safety_bump_sleep_ms`.
This creates unnecessary coupling, a race condition risk, and a redundant initialization guard.

This refactor collapses all sleep/timing responsibility into `miner.cpp`. The wallet's
`updatedBlockTip()` becomes a pure wake-up signal.

---

## Design Decisions

| Question | Decision |
|---|---|
| Q1 — MTP awareness | **B.** `MsUntilNextWindow()` takes `mtp` and advances `nextBoundary` past it |
| Q2 — `pos_timio` fate | **B.** Keep as floor: `max(MsUntilNextWindow(...), pos_timio)` |
| Q3 — Extra floor | **A.** Not needed; `pos_timio` already provides a floor |
| Q4 — Regtest | **B.** No special cap; `cv_new_block` wake handles it correctly |
| Q5 — Docs | Update `agent/SafetyBump.md` and ghost-block comment in `miner.cpp` |

---

## Why We Do Not Need the Initialization Bug Fix

An initialization guard existed in `CreateNewBlock()`:

```cpp
if (pwallet->m_last_coin_stake_search_time == 0) {
    pwallet->m_last_coin_stake_search_time = GetAdjustedTimeSeconds();
    ...
}
```

This was intended to prevent staking too early on startup. It is **unnecessary** because:

- `updatedBlockTip()` already guards against IBD via `chain().isInitialBlockDownload()`.
- The staker thread only wakes when `m_new_block_arrived` is set, which only fires from
  `updatedBlockTip()` — it cannot run before the first tip notification arrives.
- The new `MsUntilNextWindow()` helper naturally delays the first attempt to the correct
  protocol boundary, including past MTP.
- The guard was masking the real issue: timing must be computed *at stake time*, not
  pre-calculated at block arrival.

---

## New Helper

Add near `UpdateTime()` in `src/node/miner.cpp`:

```cpp
int64_t MsUntilNextWindow(const Consensus::Params& consensus, int64_t mtp)
{
    int64_t now = GetAdjustedTimeSeconds();
    int64_t nextBoundary = (now & ~consensus.nStakeTimestampMask)
                         + (consensus.nStakeTimestampMask + 1);

    // Ensure the next window is valid: block timestamp must be > MTP.
    while (nextBoundary <= mtp)
        nextBoundary += (consensus.nStakeTimestampMask + 1);

    return std::max(0LL, (nextBoundary - now) * 1000);
}
```

This preserves the original safety bump's core property: the staker never sleeps toward a window
that is `<= MTP`.

---

## Code Changes

> ⚠️ **All changes must be reviewed and approved before implementation.**

### 1. `src/wallet/wallet.cpp` — `updatedBlockTip()` (lines 1585–1622)

Replace with a pure wake-up. Remove all MTP pre-calculation, window math, and
`m_safety_bump_sleep_ms` assignment:

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

### 2. `src/wallet/wallet.h` (lines 457–458)

Remove:

```cpp
// Safety bump: pre-calculated sleep time (ms) to next valid 16-second window after new block
std::atomic<int64_t> m_safety_bump_sleep_ms{0};
```

### 3. `src/node/miner.cpp` — `CreateNewBlock()`

- **Add** `MsUntilNextWindow()` helper near `UpdateTime()`.
- **Remove** the safety bump block (`nSafetyBumpSleepMs`, lines ~219–247): the while loop
  advancing `txCoinStake.nTime` and the MTP inflation stripping logic.
- **Remove** the timer initialization guard (lines ~249–254):
  `if (pwallet->m_last_coin_stake_search_time == 0) { ... }`
- **Remove** the `m_safety_bump_sleep_ms` write-back on `fPoSCancel` (lines ~308–312).
- **Update** ghost block comment: it may now legitimately fire if the helper has a bug,
  rather than "should NEVER trigger".

### 4. `src/node/miner.cpp` — `PoSMiner()`

- **Remove** the short-circuit consumer block (lines ~804–815) that reads
  `m_safety_bump_sleep_ms.exchange(0)` before block assembly.
- **Replace** all sleep paths:

| Sleep path | Old behavior | New behavior |
|---|---|---|
| `fPoSCancel` fallback | `safetyBumpSleep > 0 ? safetyBumpSleep : pos_timio` | `std::max(MsUntilNextWindow(consensus, pindexPrev->GetMedianTimePast()), (int64_t)pos_timio)` |
| Post-success rest | `(16 + rand(4)) * 1000` | `MsUntilNextWindow(consensus, pwallet->chain().getTip()->GetMedianTimePast())` |
| Non-PoS fallback (bottom) | `pos_timio` | Keep as-is (`pos_timio`); path should be unreachable for staker wallets |

### 5. `src/node/miner.h`

- Keep `DEFAULT_STAKETIMIO` — `pos_timio` is retained as a sleep floor.

---

## Documentation Updates

- `agent/SafetyBump.md` — rewrite to describe the new single-responsibility design.
- `agent/staking.md §11` — update timer / safety bump section.
- `agent/staking_probabilities.md` — update safety bump bullet.

---

## Tests to Run After Implementation

1. `make check` or equivalent unit test suite.
2. Run a staker on testnet/regtest and verify `PoSMiner` wakes/sleeps at 16-second boundaries.
3. Verify `getstakinginfo` still reports correctly.
4. Verify no regression in orphan/ghost-block behavior.

---

## Human Responsibilities

> 👤 **The following tasks must be performed by a human developer. They are outside the scope of
> AI-assisted planning and must not be delegated to an AI agent.**

- **Compiling the software** — building the codebase after changes, verifying it compiles
  without errors or warnings, and resolving any build issues.
- **Pushing code to the repository** — staging, committing, and pushing the actual source
  code changes to the branch after implementation.
- **Running tests locally** — executing the test suite on the developer's own machine and
  verifying results before opening a pull request.
- **Opening and merging pull requests** — creating the PR, requesting reviewers, addressing
  review feedback, and merging once approved.
- **Testnet/regtest staking verification** — running a live node and manually observing
  staker behavior at 16-second boundaries.

---

## Files Changed Summary

| File | Action |
|---|---|
| `src/wallet/wallet.cpp` | Simplify `updatedBlockTip()` — remove MTP pre-calc |
| `src/wallet/wallet.h` | Remove `m_safety_bump_sleep_ms` |
| `src/node/miner.cpp` | Add helper, remove safety bump, replace all sleep paths |
| `src/node/miner.h` | No change (keep `DEFAULT_STAKETIMIO`) |
| `agent/SafetyBump.md` | Rewrite for new design |
| `agent/staking.md` | Update §11 |
| `agent/staking_probabilities.md` | Update safety bump bullet |
