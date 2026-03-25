# Node Module — Agent Knowledge Base

**Scope**: `src/node/` | **Score**: 18 (52 files, node state management)

---

## Overview

Node state management layer. Contains `NodeContext`, chainstate management, mempool handling, block storage, mining/staking logic. Segregated from `src/wallet/` and `src/qt/` to enable multi-process architecture.

---

## ⚠️ CRITICAL: Bitcoin 26.x → 30.x Upgrade Context

**Current Phase**: See .sisyphus/plans/bitcoin-28.4.0-merge-plan.md v2.7

### Never-Port Features (Node Module)

| Bitcoin Core Feature | Blackcoin More Status | Action |
|---------------------|----------------------|--------|
| RBF in mempool | Completely disabled | Never enable RBF checks |
| Dynamic fee estimation | Static 100k sat/kvB | Never port fee estimation |
| GetAdjustedTime() removal | Required for PoS | Never remove from miner |
| Block pruning | Removed | Never enable pruning |

### CRITICAL: GetAdjustedTimeSeconds() in Miner

**Location**: `src/node/miner.cpp:69`

```cpp
// BLACKCOIN-SPECIFIC: GetAdjustedTimeSeconds() preserved for PoS
int64_t nNewTime = std::max<int64_t>(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTimeSeconds());
```

**⚠️ WARNING**: Bitcoin Core replaced `GetAdjustedTime()` with `GetTime()`. This MUST be preserved for PoS block timestamp validation.

### PoS-Specific Mining Code

```cpp
// src/node/miner.cpp - Lines 6-12
// CRITICAL DIFFERENCES FROM BITCOIN CORE:
// - GetAdjustedTimeSeconds(): REQUIRED for PoS block timestamping
// - PoS kernel: Uses nStakeModifier from CBlockIndex (not in Bitcoin 30.x)
// - Static fees: 100,000 sat/kvB - no fee estimation needed
// - RBF: DISABLED - Bitcoin Core's coinbase selection not applicable
```

### Staking Logging

All staking events use `BCLog::COINSTAKE` category (`-debug=coinstake`):

| Log | Location | Description |
|-----|----------|-------------|
| `Safety Bump` | `miner.cpp:245-275` | Sleep until next valid window (fallback path) |
| `Pre-calculated` | `wallet.cpp:1562` | Using pre-calculated sleep from `updatedBlockTip()` |
| `Stripping MTP inflation` | `wallet.cpp:1556`, `miner.cpp:267` | Modulo operation stripping artificial inflation |
| `Wake-up Race Fix` | `miner.cpp:867` | Clears stale wake-up flag at loop top to prevent race condition |
| `Wallet timer` | `miner.cpp:281-307` | Per-wallet timer init/update |
| `COINSTAKE CREATED` | `miner.cpp:297` | Kernel found successfully |
| `GHOST BLOCK DETECTED` | `miner.cpp:314` | Kernel found but timestamp invalid — **should never occur with safety bump** |

**Safety Bump MTP Inflation Mitigation**:

The Safety Bump now strips artificial MTP inflation using modulo arithmetic. Attackers with +14 second clocks inflate MTP, causing naive sleep calculations to oversleep (30s instead of 16s). The modulo fix ensures both honest nodes and attackers wake at the same wall-clock moment:

```cpp
// miner.cpp:267-272 (fallback path)
if (nSafetyBumpSleepMs > 16000) {
    nSafetyBumpSleepMs %= 16000;
    if (nSafetyBumpSleepMs == 0) nSafetyBumpSleepMs = 16000;
}
```

---

## Structure

```
src/node/
├── miner.cpp, miner.h           # PoS mining/staking (914 lines)
├── context.cpp, context.h       # NodeContext definition
├── chainstate.cpp, chainstatemanager_args.cpp  # Chain state
├── mempool_args.cpp             # Mempool configuration
├── blockstorage.cpp             # Block file management
├── transaction.cpp              # Tx broadcasting
├── kernel_notifications.cpp     # Event notifications
├── interface_ui.cpp             # UI interface
├── psbt.cpp                     # PSBT handling
├── utxo_snapshot.cpp            # UTXO snapshots
└── mini_miner.cpp               # Mining utilities
```

---

## Where to Look

| Task | File | Notes |
|------|------|-------|
| **Staking/PoW** | `miner.cpp` | CreateCoinStake, UpdateTime |
| **Node context** | `context.h` | NodeContext struct |
| **Chain state** | `chainstate.cpp` | ChainstateManager |
| **Mempool** | `mempool_args.cpp` | Mempool configuration |
| **Block storage** | `blockstorage.cpp` | Block file I/O |
| **Notifications** | `kernel_notifications.cpp` | Events/hooks |

---

## Key Symbols

| Symbol | Type | Role |
|--------|------|------|
| `NodeContext` | struct | Global node state |
| `ChainstateManager` | class | Chain state coordination |
| `UpdateTime` | function | PoS-aware block time update |
| `RegenerateCommitments` | function | Witness commitment regeneration |

---

## Architecture Notes

### Segregation Principle

Code in `src/node/` should NOT call:
- `src/wallet/` directly (use `src/interfaces/`)
- `src/qt/` directly (use `src/interfaces/`)

This enables:
1. Wallet/GUI running in separate processes
2. Isolation of node operation from wallet changes
3. Future repository separation

### NodeContext Components

```cpp
// src/node/context.h
struct NodeContext {
    std::unique_ptr<kernel::Context> kernel;
    std::unique_ptr<CConnman> connman;
    std::unique_ptr<PeerManager> peerman;
    std::unique_ptr<CTxMemPool> mempool;
    ChainstateManager* chainman;
    // ... wallet pointer if enabled
};
```

---

## ⚠️ CMake Migration (29.x → 30.x)

**When migrating node module to CMake**:
- Preserve `GetAdjustedTimeSeconds()` calls in miner
- Preserve PoS-specific staking logic
- Never enable RBF in mempool configuration
- Never enable block pruning

---

## Anti-Patterns (THIS MODULE)

- **NEVER**: Enable RBF in mempool (`mempoolreplacement=0`)
- **NEVER**: Port fee estimation code from Bitcoin Core
- **NEVER**: Remove `GetAdjustedTimeSeconds()` from miner
- **NEVER**: Enable block pruning (staking needs full chain)
- **NEVER**: Call wallet/Qt code directly (use interfaces)

---

*Generated by /init-deep*