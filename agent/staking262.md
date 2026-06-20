# Blackcoin More v26.2.0 Staking Walkthrough

## 1. Entry Points: Start/Stop (`wallet/staking.cpp`)

`StartStake()`: validates the wallet (has private keys, not blank, keypool populated), sets `m_enabled_staking=true`, then calls `StakeCoins(wallet, true)`.

`StakeCoins()`: joins any existing staker thread, then spawns a new `ThreadStakeMiner` thread for this wallet.

`StopStake()`: sets `m_stop_staking_thread=true`, `m_enabled_staking=false`, joins/kills the thread.

---

## 2. ThreadStakeMiner → PoSMiner (`node/miner.cpp`)

`ThreadStakeMiner()` wraps `PoSMiner(pwallet)` in an infinite retry loop — if `PoSMiner` throws, it catches, logs, and restarts. This is the outer crash-recovery shell.

---

## 3. PoSMiner — the main loop (`node/miner.cpp:833-921`)

```cpp
void PoSMiner(CWallet *pwallet)
```

**Phase A — Setup (one-time at thread start):**

Resolves the staking destination — looks up a "Staking Legacy Address" in the address book; if not found, creates one via `GetNewDestination(LEGACY, "Staking Legacy Address")`.

Computes `pos_timio`:
```cpp
pos_timio = gArgs.GetIntArg("-staketimio", DEFAULT_STAKETIMIO) + 30 * sqrt(UTXOs);
// DEFAULT_STAKETIMIO = 500 (ms)
```
Scans all available UTXOs for staking (calls `AvailableCoinsForStaking`) just to count them for the formula.

**Phase B — Infinite loop:**

```
while (true) {
    1. Wait while wallet locked / staking disabled / reindex / importing
       → SleepStaker(pwallet, 5000)

    2. Wait until at least 1 peer connected AND IBD complete
       → SleepStaker(pwallet, 10000)

    3. Wait until sync ≥ 99.6%
       → SleepStaker(pwallet, 10000)

    4. Remember current tip: pindexPrev = chain().getTip()
    
    5. Call CreateNewBlock(pwallet, &fPoSCancel, ...)
       LOCK2(cs_wallet, cs_main)

       If CreateNewBlock returns nullptr:
           if fPoSCancel → sleep pos_timio, continue (no block found)
           else → error (keypool ran out), sleep 10s, return

    6. If block found (IsProofOfStake):
           SignBlock() → ProcessBlockFound() → sleep 16-20s

    7. Sleep pos_timio, continue
}
```

**Critical observation:** steps 5, 6, and 7 each have sleeps. The `pos_timio` sleep at step 7 runs even after a block is found. Steps 6 and 7 combined give `pos_timio + 16-20s` rest after a successful block.

---

## 4. SleepStaker — interruptible sleep (`node/miner.cpp:806-829`)

```cpp
bool SleepStaker(CWallet *pwallet, uint64_t milliseconds) {
    uint64_t seconds = milliseconds / 1000;
    milliseconds %= 1000;
    for (unsigned int i = 0; i < seconds; i++) {
        if(!pwallet->IsStakeClosing()) Sleep(1s);
        else return false;  // abort
    }
    if (milliseconds) {
        if(!pwallet->IsStakeClosing()) Sleep(milliseconds);
        else return false;
    }
    return !pwallet->IsStakeClosing();
}
```

`IsStakeClosing()` checks `shutdownRequested() || m_stop_staking_thread`. Sleep is broken into 1-second chunks so the thread can respond to shutdown within 1 second. Returns `false` if staking should stop → `PoSMiner` returns → `ThreadStakeMiner` restarts.

**In v26.x, there is NO wake-on-block mechanism.** The staker **always sleeps the full duration**. No `cv_new_block`, no `m_safety_bump_sleep_ms`, no short-circuit.

---

## 5. CreateNewBlock — the timer guard (`node/miner.cpp:213-294`)

This is the heart of the staking timing:

```cpp
// The guard — STATIC LOCAL, shared by ALL wallets
static int64_t nLastCoinStakeSearchTime = GetAdjustedTimeSeconds();

if (pwallet) {
    *pfPoSCancel = true;
    pblock->nBits = GetNextTargetRequired(...);

    CMutableTransaction txCoinStake;
    txCoinStake.nTime &= ~nStakeTimestampMask;    // mask to boundary
    // BUG: nTime is uninitialized garbage!

    int64_t nSearchTime = txCoinStake.nTime;

    if (nSearchTime > nLastCoinStakeSearchTime) {
        if (CreateCoinStake(..., 1, txCoinStake, ...)) {
            if (txCoinStake.nTime >= pindexPrev->MTP+1) {
                // Found a kernel!
                *pfPoSCancel = false;
            }
        }
        pwallet->m_last_coin_stake_search_interval =
            nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }

    if (*pfPoSCancel) return nullptr;
}
```

**Key behaviors:**

1. **nSearchInterval = 1** is hardcoded in the call to `CreateCoinStake`. The backward search (`for n=0; n<min(1,60); n++`) only runs `n=0` — searches **exactly** the boundary timestamp, no backward search.

2. **Timer guard:** `nSearchTime > nLastCoinStakeSearchTime`. Since `nSearchTime` is masked to a 16s boundary, this guard fires **at most once per 16s window**. Once it fires, `nLastCoinStakeSearchTime = nSearchTime` advances to the current boundary. The next call with the same boundary mask fails the guard.

3. **BUG in 26.x:** `txCoinStake.nTime` is **uninitialized garbage**. `nSearchTime = garbage & ~0xf`. The guard `garbage > last` has ~50% chance of passing on the first call. The masked garbage value is passed to `CreateCoinStake` as `txNew.nTime` but `CreateCoinStake` wipes it when it finds a kernel (`txNew.nTime -= n`), so the staked timestamp is correct — only the guard's first-call behavior is random.

4. **Static local = global:** `nLastCoinStakeSearchTime` is shared across all wallets. Multi-wallet staking serializes: only one wallet's `CreateNewBlock` call passes the guard per 16s window.

5. **m_last_coin_stake_search_interval** is logged/recorded per-wallet for `getstakinginfo` display, but the actual guard uses the static local, not the member. The member is write-only in this path.

---

## 6. CreateCoinStake — the kernel search (`wallet/staking.cpp:252-476`)

```cpp
bool CreateCoinStake(CWallet& wallet, unsigned int nBits,
    int64_t nSearchInterval, CMutableTransaction& txNew,
    CAmount& nFees, CTxDestination destination)
```

**Step 1 — Select coins:**
```cpp
SelectCoinsForStaking(wallet, nAllowedBalance, setCoins, nValueIn)
```
Calls `AvailableCoinsForStaking` then greedily picks coins up to `nTargetValue = nBalance - m_reserve_balance`:
- If coin ≥ target: take it and stop
- Else if coin < target + 1 BLK: take it
This is a simple greedy, NOT the knapsack used for send transactions.

`AvailableCoinsForStaking` filters: depth ≥ max(1, coinbaseMaturity=500), depth ≤ 999999, value ≥ `m_min_staking_amount` (default 0.1 BLK), spendable, not locked, not spent, not immature, not conflicted.

**Step 2 — Kernel search (backward loop):**
```cpp
for (unsigned int n=0; n < min(nSearchInterval, 60); n++) {
    // nSearchInterval = 1, so only n=0 runs
    COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
    if (CheckKernel(pindexPrev, nBits, txNew.nTime - n, prevoutStake, ...)) {
        // FOUND!
    }
}
```

With `nSearchInterval=1`, only `n=0` is checked — `txNew.nTime` (the masked boundary). No backward search.

For each UTXO, calls `CheckKernel` which:
1. Looks up the coin in CCoinsViewCache
2. Checks maturity (≥ 500 confirmations)
3. Finds the block the coin was created in (GetAncestor)
4. Calls `CheckStakeKernelHash`

**Step 3 — Add extra inputs:**
If kernel found, adds up to 10 additional inputs with the same scriptPubKey, up to `GetStakeCombineThreshold` (500 BLK).

**Step 4 — Split/donate:**
- If total credit ≥ 1000 BLK, creates 2 outputs (split)
- If donation enabled (dev fund, default 20%), adds dev output

**Step 5 — Sign:**
Legacy: `SignSignature` for each input
Descriptors: `wallet.SignTransaction`

---

## 7. CheckStakeKernelHash — the kernel hash (`pos.cpp:77-122`)

```cpp
bool CheckStakeKernelHash(pindexPrev, nBits, blockFromTime,
    prevoutValue, prevout, nTimeTx)
```

Computes:
```
hash = SHA256(nStakeModifier + blockFromTime + prevout.hash + prevout.n + nTimeTx)
target = nBits_target * nValueIn * COIN
```

If `hash < target`, it's a kernel.

**Determinism:** Same `(nStakeModifier, blockFromTime, prevout, nTimeTx)` always produces the same hash. `nStakeModifier` changes every block. `nTimeTx` is the search variable — the masked boundary (e.g., 1234567890 & ~0xf = 1234567888). Since `nSearchInterval=1`, each UTXO gets exactly one hash attempt per 16s window.

---

## 8. CheckProofOfStake — block validation (`pos.cpp:136-193`)

Called during `ProcessBlockFound` (miner self-check) and during block validation from peers.

Checks:
1. First input is a valid coin in UTXO set
2. Maturity ≥ 500 confirmations
3. Block ancestor found
4. `VerifySignature(coinPrev, ..., tx, 0, SCRIPT_VERIFY_NONE)` — signature check with NO flags
5. `CheckStakeKernelHash` with `nTimeTx = coinstake.nTime ?: block.nTime`

**SCRIPT_VERIFY_NONE** means:
- P2PK/P2PKH kernels: signature verified by bare CHECKSIG (works fine)
- P2WPKH/P2SH/P2TR kernels: **signature NOT verified** — `VerifySignature` passes null witness and no P2SH redeem script, so the flags would reject them. With `NONE`, the checks pass trivially.

---

## 9. CheckKernel + CStakeCache (`pos.cpp:207-265`)

`CheckKernel` has two overloads — with and without cache:

Without cache: full lookup — `GetCoin()` → maturity → `GetAncestor()` → `CheckStakeKernelHash`.

With cache: if `prevout` found in cache map, uses cached `(blockFromTime, amount)` directly, then **double-checks** by calling the uncached version (safety vs reorgs).

`CacheKernel` pre-populates the cache for the currently selected UTXO set. Called from `CreateCoinStake` before the search loop.

---

## 10. CoinStake Timestamp Rules (`pos.cpp:27-38`)

```cpp
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx) {
    if (IsProtocolV2(nTimeBlock))
        return (nTimeBlock == nTimeTx) && ((nTimeTx & 0xf) == 0);
    else
        return (nTimeBlock == nTimeTx);
}
```

Protocol V2+ (active since 2014): block nTime must equal coinstake nTime, and both must have the bottom 4 bits cleared (boundary alignment). This means only timestamps like `...0, ...16, ...32` are valid.

---

## 11. Block Timestamp Checks (`validation.cpp`)

**FutureDrift** (`validation.cpp:143-148`):
```cpp
int64_t FutureDrift(Chainstate& active_chainstate, int64_t nTime) {
    if (regtest) return nTime + 24h;
    return IsProtocolV2(nTime) ? nTime + 15 : nTime + 10min;
}
```

**Block timestamp checks** (`ContextualCheckBlock`, `CheckBlock`):
- `block.nTime > FutureDrift(adjustedTime)` → reject "time-too-new" (15s tolerance)
- `CheckCoinStakeTimestamp(block.nTime, coinstake.nTime)` → reject if mismatch

**Transaction timestamp** (`AcceptToMemoryPool`):
- `nTimeTx > FutureDrift(adjustedTime)` → reject if >15s future
- For v2 tx (`nTime=0`), uses `GetAdjustedTimeSeconds()` as nTimeTx

---

## 12. ProcessBlockFound (`miner.cpp:786-803`)

```cpp
static bool ProcessBlockFound(const CBlock* pblock, ChainstateManager& chainman) {
    // 1. CheckProofOfStake — verify kernel
    // 2. Check if stale: hashPrevBlock != activeTip
    // 3. chainman.ProcessNewBlock — submit to node
}
```

**Stale check:** If tip advanced since `CreateNewBlock` read it, the block is rejected. This is the counterattack-window problem — the honest node cannot respond to a competitor at the same height because the tip changed.

---

## 13. Block Notification Handlers (`wallet.cpp`)

**blockConnected:** `SyncTransaction` for each tx → wallet tracking. Sets `m_last_block_processed`.

**blockDisconnected:** `SyncTransaction` (marks unconfirmed), resolves conflicts per input, then calls `AbandonOrphanedCoinstakes`.

**AbandonOrphanedCoinstakes:** Iterates all wallet txns, abandons any coinstake with `depth=0` (not in main chain) that isn't already abandoned. This cleans up orphaned coinstakes that never made it into a block.

**updatedBlockTip:** In 26.x, just `m_best_block_time = GetTime()`. **No wake-on-block.**

---

## 14. v28-CORE vs 26.x Differences Summarized

| Feature | 26.x | v28-CORE |
|---|---|---|
| Timer guard variable | `static` local in `CreateNewBlock` (global) | Per-wallet `m_last_coin_stake_search_time` |
| `nSearchTime` init | Garbage (`nTime` uninitialized) | `GetAdjustedTimeSeconds()` |
| Wake-on-block | None | `cv_new_block` + `updatedBlockTip` sets `m_new_block_arrived` |
| Safety bump post-block | Hardcoded 16-20s sleep | Computed `m_safety_bump_sleep_ms` |
| Short-circuit sleep | None | Reads `m_safety_bump_sleep_ms` before `CreateNewBlock` |
| `AbandonOrphanedCoinstakes` | Called after blockDisconnected | Same (unchanged) |
| `pos_timio` formula | Same (500 + 30√UTXOs) | Same |
| `m_last_coin_stake_search_interval` | Write-only in miner (logged) | Same |

---

## 15. The Sleep Diagram (26.x)

```
PoSMiner:
  │
  ├── [wallet locked] ──→ SleepStaker(5000ms) ──→ loop
  ├── [no peers/IBD] ──→ SleepStaker(10000ms) ──→ loop
  ├── [not synced] ────→ SleepStaker(10000ms) ──→ loop
  │
  ├── CreateNewBlock()
  │     │
  │     ├── guard fails (same window) → return null, fPoSCancel=true
  │     ├── guard passes → CreateCoinStake → no kernel → return null, fPoSCancel=true
  │     └── guard passes → CreateCoinStake → kernel found → return block
  │
  ├── [fPoSCancel=true] ──→ SleepStaker(pos_timio) ──→ loop
  ├── [block found] ──────→ SignBlock → ProcessBlockFound → SleepStaker(16000-20000ms) → 
  │                           SleepStaker(pos_timio) ──→ loop
  └── (no block, fPoSCancel=false — shouldn't happen)
```

The timer guard is the only thing preventing 100% CPU usage. Without it, `pos_timio ≈ 1391ms` would cause ~43 CreateNewBlock calls per 16s window. With the guard, ~1 call succeeds per window and ~10 calls fail at the guard (each sleeping `pos_timio` between attempts).

`pos_timio` serves as a **minimum inter-call delay floor** — it prevents tight-looping if the guard ever fails (e.g., clock rewind, first-call bug). Under normal operation, the guard is the primary throttle.

---

## 16. Efficiency Analysis

### Where it wastes cycles

| Component | Waste factor |
|---|---|
| **pos_timio loop** | ~11 iterations per 16s window (`1391ms` each). Guard passes **1 time**, fails **10 times**. 90% of `CreateNewBlock` calls do nothing but hit the guard. |
| **No wake-on-block** | New block arrives → staker keeps sleeping `pos_timio` (1-2s). Misses the fresh boundary; only wakes on next timer tick. |
| **AvailableCoinsForStaking** | Full wallet scan (`mapWallet` iteration) **every** `CreateCoinStake` call. For 885 UTXOs, ~500-1000 `CWalletTx` objects touched per attempt. |
| **SelectCoinsForStaking** | Greedy linear scan over selected coins — negligible. |
| **CheckKernel (no cache hit)** | `GetCoin` (LevelDB) + `GetAncestor` (skip list climb) per UTXO. With cache: skips DB but **double-checks** uncached anyway. |
| **Multi-wallet** | Static guard serializes all wallets — only one gets the window. Others burn `pos_timio` sleeps for nothing. |

### What saves it from being catastrophic

1. **Timer guard** — without it, 43 calls/window instead of ~11.
2. **nSearchInterval=1** — only 1 hash per UTXO per window (Peercoin would do up to 60).
3. **CStakeCache** — avoids repeat `GetCoin`/`GetAncestor` within the same `CreateCoinStake` call.

### Rough cost per 16s window (885 UTXOs)

| Operation | Count | Cost |
|---|---|---|
| `CreateNewBlock` entry | 11 | lock contention (`cs_wallet` + `cs_main`) |
| `AvailableCoinsForStaking` | 1 (when guard passes) | ~885 iterations, lock held |
| `CreateCoinStake` UTXO loop | 1 × 885 | `CheckKernel` × 885 |
| `CheckKernel` (cache miss) | ~885 | LevelDB read + ancestor climb |
| `CheckStakeKernelHash` | ~885 | 1 SHA256 |

**Total**: ~1-2ms actual CPU work per window, but **~15s of wall time** spent sleeping/waiting.

### Comparison: v28-CORE refactor improves this by

- **Wake-on-block**: 0ms latency to new window instead of `pos_timio` average
- **Per-wallet guard**: no cross-wallet serialization
- **MsUntilNextWindow**: precise sleep to boundary, no polling loop
- **Eliminated double-sleep bug** (the `m_new_block_arrived` flag leak)

### Bottom line

The 26.x design is **CPU-light but latency-heavy**. It works because PoS difficulty makes hits rare — most windows yield nothing anyway. The refactor in `StakerTimingRefactor_Diff.md` removes the polling waste and cross-wallet contention.

---

## 17. Timer Guard Deep Dive: Why ~11 calls but only 1 passes

The guard and the polling loop are decoupled:

### PoSMiner loop (every `pos_timio` ≈ 1391ms)
```
Thread starts
    ↓
Ready checks (may sleep 5s/10s chunks)
    ↓
CreateNewBlock #1  ← IMMEDIATE, no pos_timio sleep before
    ↓ (guard fails, fPoSCancel=true)
SleepStaker(pos_timio)  ← FIRST pos_timio sleep
    ↓
CreateNewBlock #2
    ↓
... 11 total calls per window ...
```

### CreateNewBlock timer guard (the 16s boundary)
```cpp
static int64_t nLastCoinStakeSearchTime = GetAdjustedTimeSeconds();  // ~1.7B

txCoinStake.nTime &= ~nStakeTimestampMask;  // mask to boundary
int64_t nSearchTime = txCoinStake.nTime;    // garbage & ~0xf initially

if (nSearchTime > nLastCoinStakeSearchTime) {  // passes only when boundary advances
    // kernel search runs
    nLastCoinStakeSearchTime = nSearchTime;    // advances to boundary
}
```

### Timeline of one 16s window
```
t=0ms      CreateNewBlock #1: nSearchTime = garbage_boundary(0) = 0
           guard: 0 > 1.7B? FALSE → fPoSCancel=true
           SleepStaker(1391ms)

t=1391ms   CreateNewBlock #2: nSearchTime = 0
           guard: FALSE
           SleepStaker(1391ms)

... (8 more failures) ...

t≈12519ms  CreateNewBlock #10: nSearchTime = 0
           guard: FALSE
           SleepStaker(1391ms)

t≈13910ms  CreateNewBlock #11: wall clock crossed boundary
           nSearchTime = boundary(16000) = 16000
           guard: 16000 > 1.7B? FALSE (still!)

t≈15301ms  CreateNewBlock #12 (next window)
           nSearchTime = 16000
           guard: 16000 > 0? TRUE → PASSES!
           nLastCoinStakeSearchTime = 16000
```

**Key insight**: PoSMiner has **zero awareness** of the 16s window. It blindly polls every `pos_timio`. The guard in `CreateNewBlock` filters 10/11 calls. The single call that happens to land after a boundary crossing gets through.

---

## 18. ThreadStakeMiner — Crash Wrapper + Lifecycle Manager

```cpp
// miner.cpp:923-935
void static ThreadStakeMiner(CWallet *pwallet) {
    pwallet->WalletLogPrintf("ThreadStakeMiner started\n");
    while (true) {
        try {
            PoSMiner(pwallet);           // ← runs until it returns or throws
            break;                       // ← normal exit (staking stopped)
        }
        catch (std::exception& e) {
            PrintExceptionContinue(&e, "ThreadStakeMiner()");
        } catch (...) {
            PrintExceptionContinue(nullptr, "ThreadStakeMiner()");
        }
    }
    pwallet->WalletLogPrintf("ThreadStakeMiner stopped\n");
}
```

| Responsibility | Code |
|---|---|
| **Start log** | `"ThreadStakeMiner started"` |
| **Run PoSMiner** | Calls `PoSMiner(pwallet)` |
| **Catch crashes** | Any exception → log → **restart loop** (not exit) |
| **Normal stop** | `PoSMiner` returns → `break` → `"ThreadStakeMiner stopped"` |

### Full thread hierarchy
```
main thread
    │
    ├── StakeCoins(fStake=true)         // wallet/staking.cpp:26
    │       │
    │       └── std::thread(ThreadStakeMiner, pwallet)
    │               │
    │               └── ThreadStakeMiner()
    │                       │
    │                       └── while (true) {
    │                               try { PoSMiner(pwallet); break; }
    │                               catch (...) { log, continue; }
    │                           }
    │
    └── (other threads: net, validation, etc.)
```

### When does `PoSMiner` return (causing restart)?

| Path | Reason |
|---|---|
| `SleepStaker` returns `false` | `IsStakeClosing()` = shutdown or `m_stop_staking_thread` |
| `return` after keypool error | Keypool empty, logs error, returns |
| Exception thrown | Any uncaught exception in `PoSMiner` → caught by wrapper → restart |

---

## 19. Thread Lifecycle: Not "Always Active"

The thread only exists when staking is enabled.

### StartStake → spawns thread
```cpp
void StartStake(CWallet& wallet) {
    // validation: not disable_private_keys, not blank, keypool has keys
    wallet.m_enabled_staking = true;
    StakeCoins(wallet, wallet.m_enabled_staking);   // ← SPAWNS THREAD
}

void StakeCoins(CWallet& wallet, bool fStake) {
    // Join existing thread first
    if (wallet.threadStakeMinerGroup) {
        for (auto& t : *wallet.threadStakeMinerGroup)
            if (t.joinable()) t.join();
        wallet.threadStakeMinerGroup->clear();
    }

    if (fStake) {
        wallet.threadStakeMinerGroup = make_unique<vector<thread>>();
        wallet.threadStakeMinerGroup->emplace_back(ThreadStakeMiner, &wallet);
    }
}
```

### StopStake → joins & destroys thread
```cpp
void StopStake(CWallet& wallet) {
    if (!wallet.threadStakeMinerGroup) { ... }
    else {
        wallet.m_stop_staking_thread = true;   // 1. Signal
        wallet.m_enabled_staking = false;
        StakeCoins(wallet, false);              // 2. JOIN (blocks here)
        wallet.threadStakeMinerGroup = 0;
        wallet.m_stop_staking_thread = false;
    }
}
```

### States

| State | Thread exists? | `m_enabled_staking` | `m_stop_staking_thread` |
|---|---|---|---|
| Wallet loaded, staking off | ❌ No | `false` | `false` |
| User runs `staking true` | ✅ Yes | `true` | `false` |
| User runs `staking false` | ⏳ Joining... | `false` | `true` |
| After stop completes | ❌ No | `false` | `false` |
| Shutdown | ✅ Yes (briefly) | `false` | `true` |

### "Joining..." blocking behavior

`StopStake` **blocks until thread finishes**:

```cpp
// StakeCoins(false):
for (std::thread& thread : *wallet.threadStakeMinerGroup)
    if (thread.joinable()) thread.join();  // ← BLOCKS HERE
```

1. `m_stop_staking_thread = true` → signal visible to `IsStakeClosing()`
2. `thread.join()` — **caller blocks**
3. Inside `PoSMiner`: next `SleepStaker` sees `IsStakeClosing()=true`, returns `false`
4. `PoSMiner` returns → `ThreadStakeMiner` breaks → thread exits
5. `join()` unblocks — thread dead, resources cleaned up

**Worst case join time**: ~1.4s (one `pos_timio` chunk). Typical: < 100ms.

The RPC `staking false` call won't return until the join completes.

---

## 20. PoSMiner Pauses Inside, Thread Stays Alive

When wallet locked / no peers / IBD / not synced:
- **Thread stays alive** (blocked in `SleepStaker`)
- `PoSMiner` loops in `SleepStaker(5000/10000)` chunks
- Thread exits **only** when `m_stop_staking_thread=true` (set by `StopStake` or shutdown)

So: **Thread = always alive while staking enabled. PoSMiner = actively polling only when conditions met.**
