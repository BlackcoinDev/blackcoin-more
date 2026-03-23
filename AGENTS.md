# Blackcoin More — Agent Knowledge Base

**Generated:** 2026-03-22
**Commit:** (current)
**Branch:** (current)
**Project:** Blackcoin More (PoS cryptocurrency fork from Bitcoin Core)

---

## ⚠️ CRITICAL: Bitcoin 26.x → 30.x Upgrade Context

### Current Upgrade Status (March 2026)

| Phase | Status | Version | Notes |
|-------|--------|---------|-------|
| Phase 1: v27.x | ✅ COMPLETE | 27.2.0 | C++20, GetAdjustedTime preserved |
| Phase 2: v28.x | 🏗️ IN PROGRESS | - | GenTxid partial, DataStream partial |
| Phase 3: v29.x | ⏳ NOT STARTED | - | GenTxid→variant refactor |
| Phase 4: v30.x | ⏳ NOT STARTED | - | CMake migration, uint256 cleanup |

### Upgrade Progress

| Component | 27.x Status | 28.x Status | Notes |
|-----------|-------------|-------------|-------|
| GetAdjustedTime() | ✅ Preserved | ✅ Ready | UPGRADE NOTE markers added |
| nStakeModifier | ✅ Preserved | ✅ Ready | chain.h:230 |
| nFlags/vchBlockSig | ✅ Preserved | ✅ Ready | block.h |
| SegWit (80%) | ✅ Activated | ✅ Ready | Height 5805000 |
| GenTxid/Txid | - | 🏗️ Partial | Used in validation.cpp |
| DataStream | - | 🏗️ Partial | Preferred in new code |
| Staking Cache | ✅ Complete | ✅ Ready | 3-layer architecture |

| Feature | Bitcoin Core | Blackcoin More | Never Port |
|---------|--------------|----------------|------------|
| **RBF** | Enabled | **DISABLED** | ✅ Never enable |
| **Fees** | Dynamic | **Static (100k sat/kvB)** | ✅ Never port estimation |
| **BDB** | Removed in 30.x | **REQUIRED (6.2)** | ✅ Never remove |
| **GetAdjustedTime()** | Removed in 28.x | **REQUIRED** | ✅ Never remove |
| **PoS Block Header** | Standard | **Extended** | ✅ Preserve exactly |
| **SegWit Threshold** | 95% | **80%** | ✅ Keep 80% |
| **Taproot** | Active | **NEVER_ACTIVE** | ✅ Never enable |
| **C++ Standard** | C++20 | C++17 → C++20 | Upgrade required |
| **Build System** | CMake | Autotools | Migration planned |
| **Block Pruning** | Available | **REMOVED** | ✅ Never implement |
| **nStakeModifier** | Not present | **REQUIRED** | ✅ Must preserve |
| **OP_RETURN Stake** | Banned/Unsupported | **SUPPORTED** | ✅ Segment Leader |

### Never-Port List (Critical)

1. **❌ RBF Implementation**
   - Blackcoin uses first-seen rule only
   - `mempoolreplacement` policy must remain disabled
   - Never add `opt_in_rbf` transaction signaling

2. **❌ Dynamic Fee Estimation**
   - Static 100,000 sat/kvB only
   - Never port `fee_estimates.dat` system
   - Never implement `estimateSmartFee`

3. **❌ BDB Removal**
   - Bitcoin 30.x removed BDB wallet support
   - Blackcoin More MUST preserve BDB 6.2
   - Never force SQLite/descriptor migration

4. **❌ GetAdjustedTime() Removal**
   - Critical for PoS kernel validation (removed in Bitcoin 28.x)
   - Never replace with `GetTime()` only
   - Never remove from `src/timedata.h`

5. **❌ Taproot/Script Versions 1+**
   - Taproot marked `NEVER_ACTIVE`
   - Never enable taproot deployment
   - Never implement future script versions

6. **❌ nStakeModifier Loss**
   - CBlockIndex::nStakeModifier is CRITICAL for PoS
   - NOT present in Bitcoin Core
   - Used in `src/pos.cpp:CheckStakeKernelHash()`
   - Must preserve this field exactly

7. **❌ Block Pruning**
   - Staking requires access to complete blockchain history
   - Never implement block file pruning
   - Never add `-prune` argument or pruning UI
   - Complete blockchain must always be available for PoS validation

### SegWit Status (March 2026)

- **Testnet**: ✅ ACTIVATED September 2024 (80% BIP-9 threshold)
- **Mainnet**: ✅ ACTIVATED March 2026 at height 5805000 (80% BIP-9 threshold)
- **Critical**: 80% threshold is hardcoded, different from Bitcoin's 95%

### Staking Performance Architecture (3-Layer Cache)

**Status**: Implemented in v27.2.0 (Jan 2026). Future agents MUST maintain this structure:

1. **Layer 0: Zero Disk I/O**: removed `g_txindex` reads in `CreateCoinStake`. **NEVER** re-introduce disk lookups in the staking loop.
2. **Logic Cache (`m_cached_spks`)**: Maps Script→Descriptor. Must reside in `CWallet`.
3. **Data Cache (`stakeCache`)**: Cache of UTXO timestamps. Must reside in `CWallet`.
4. **Key Cache (`GetPubKey` Zero-Alloc)**: Direct map access in `DescriptorScriptPubKeyMan`. **NEVER** revert to `GetSolvingProvider` for staking loops.
5. **Safety Bump Pre-Calculation (v27.2.0+)**: Pre-calculates next staking window using `GetAdjustedTimeSeconds()` in `updatedBlockTip()`. Strips MTP inflation attacks via modulo 16000 (`sleepMs %= 16000`). Uses same time reference as block validation. Local policy only — does not change consensus.
6. **Ghost Block Logging (v27.2.0+)**: Logs dropped kernels due to timestamp validation. NOTE: With Safety Bump, ghost blocks should NEVER occur — logging retained for debugging only.

### Recommended Upgrade Strategy

**Incremental Approach (Recommended)**:

```
26.x → 27.x → 28.x → 29.x → 30.x
```

Each step smaller, easier to test and debug issues.

**Key Milestones**:

- 27.x: GetAdjustedTime() removed (must preserve in Blackcoin)
- 28.x: Txid/Wtxid types, CDataStream→DataStream
- 29.x: GenTxid→std::variant
- 30.x: uint256→Txid final, BDB conditional compilation, CMake migration

---

## Upgrade Documentation References

When working on the upgrade, reference these files for critical information:

| File | Purpose |
|------|---------|
| `UPGRADE.md` | Complete upgrade plan with phase details |
| `agent/BLOCK_SERIALIZATION.md` | **CRITICAL**: PoS block serialization (nFlags, vchBlockSig, nStakeModifier) |
| `agent/CMake_MIGRATION.md` | Build system migration (29.x → 30.x) |
| `agent/DESCRIPTOR_STAKING.md` | **Staking analysis** (Legacy vs Descriptor wallets) |
| `agent/STAKECACHE.md` | **Stake cache documentation** with Qtum cross-reference |
| `src/primitives/block.h` | PoS header fields (nFlags, vchBlockSig) |
| `src/chain.h` | nStakeModifier field (CRITICAL) |
| `src/pos.h` / `src/pos.cpp` | PoS kernel validation |
| `src/timedata.h` | GetAdjustedTime() (must preserve) |
| `src/policy/policy.h` | Static fee constants |
| `src/crypto/scrypt.cpp` | SSE2 for legacy PoW |
| `src/kernel/chainparams.cpp` | Network ports, config |
| `configure.ac` | BDB default = yes (changed from auto) |

---

## Overview

Blackcoin More is a Proof-of-Stake 3.1 (PoSV3/BPoS) cryptocurrency derived from Bitcoin Core. C++17 codebase with Qt5 GUI, built via GNU Autotools. Multi-binary architecture: daemon (`blackmored`), CLI (`blackmore-cli`), wallet (`blackmore-wallet`), GUI (`blackmore-qt`), and utilities.

---

## Structure

```
./                    # Autotools root
├── configure.ac      # Build configuration (79KB)
├── Makefile.am       # Top-level makefile
├── autogen.sh        # Bootstrap script
├── agent/            # AI agent documentation (THIS REPO)
├── src/              # C++ source (159 files at root + 29 subdirs)
│   ├── kernel/       # Core: chain, mempool, validation
│   ├── wallet/       # BDB/SQLite wallets, key management
│   ├── rpc/          # JSON-RPC server implementation
│   ├── consensus/    # Protocol validation rules
│   ├── qt/           # Qt5 GUI (119 files)
│   ├── net.cpp       # P2P networking (150KB)
│   ├── validation.cpp# Block/transaction validation
│   └── init/         # Application initialization
├── test/
│   ├── functional/   # Python integration tests
│   ├── lint/         # Static analysis
│   └── unit/         # C++ Boost.Test
├── ci/               # CI scripts (Linux, macOS, Windows)
└── doc/              # Build docs, developer notes
```

---

## Where to Look

| Task | Location | Notes |
|------|----------|-------|
| **Daemon entry** | `src/bitcoind.cpp` | `main()` for blackmored |
| **CLI entry** | `src/bitcoin-cli.cpp` | `main()` for blackmore-cli |
| **Wallet entry** | `src/bitcoin-wallet.cpp` | `main()` for blackmore-wallet |
| **GUI entry** | `src/qt/bitcoin.cpp` | `main()` for blackmore-qt |
| **Consensus rules** | `src/consensus/` | Validation, merkle, tx_verify |
| **PoS implementation** | `src/pos.h`, `src/pos.cpp` | Proof-of-Stake logic |
| **PoW (legacy)** | `src/pow.cpp` | Inactive PoW code preserved |
| **P2P networking** | `src/net.cpp`, `src/net_processing.cpp` | 150KB, 298KB respectively |
| **RPC server** | `src/rpc/server.cpp`, `src/rpc/blockchain.cpp` | 129KB blockchain.cpp |
| **Wallet storage** | `src/wallet/bdb.cpp`, `src/wallet/db.cpp` | BDB/SQLite |
| **Validation** | `src/validation.cpp` | Block/tx validation |
| **Chain state** | `src/kernel/chain.h`, `src/kernel/chain.cpp` | Block index, CChain |
| **Mempool** | `src/kernel/mempool_entry.h`, `src/kernel/mempool_persist.cpp` | Tx pool |
| **Config parsing** | `src/init/*.cpp` | Startup, args |

---

## Code Map

### Entry Points (binaries)

| Binary | Source | Role |
|--------|--------|------|
| `blackmored` | `src/bitcoind.cpp` | Daemon process |
| `blackmore-cli` | `src/bitcoin-cli.cpp` | CLI tool |
| `blackmore-wallet` | `src/bitcoin-wallet.cpp` | Wallet CLI |
| `blackmore-tx` | `src/bitcoin-tx.cpp` | Tx utility |
| `blackmore-chainstate` | `src/bitcoin-chainstate.cpp` | Chain inspection |
| `blackmore-qt` | `src/qt/bitcoin.cpp` | GUI application |
| `test_blackmore` | `src/test/main.cpp` | C++ unit tests |

### Core Classes

| Symbol | Type | Location | Role |
|--------|------|----------|------|
| `CChain` | class | `src/kernel/chain.h` | Block index chain |
| `CBlockIndex` | struct | `src/chain.h` | Block metadata |
| `CValidationState` | struct | `src/consensus/validation.h` | Validation result |
| `CTxMemPool` | class | `src/txmempool.h` | Transaction pool |
| `CWallet` | class | `src/wallet/wallet.h` | Wallet storage |
| `CKeyStore` | class | `src/key.h` | Key storage |
| `CChainParams` | class | `src/kernel/chainparams.h` | Chain parameters |
| `CConnman` | class | `src/net.h` | Connection manager |

---

## Conventions

### C++ Style (Non-Standard)

- **Bracing**: Allman style (`AfterClass: true`, `AfterFunction: true`)
- **Pointers**: Left-aligned (`Type* var`, NOT `Type *var`)
- **Column limit**: None (`ColumnLimit: 0`)
- **Short functions**: Allowed on single line (`AllowShortFunctionsOnASingleLine: All`)
- **Indentation**: 4 spaces (`IndentWidth: 4`)
- **Tabs**: Never (`UseTab: Never`)

### Naming

- Classes: `PascalCase` (`CBlock`, `CWallet`, `CChain`)
- Functions: `PascalCase` (`GetBlockHash`, `ConnectBlock`)
- Variables: `snake_case` (`block_height`, `tx_fee`)
- Constants: `UPPER_CASE` (`MAX_BLOCK_SIZE`)

### Imports

Order: Standard library → Project headers → Third-party

### RAII & Smart Pointers

Prefer `std::unique_ptr`, `std::shared_ptr`. Use exceptions for exceptional cases only.

### Error Handling

- `std::optional` / `Result` types for expected errors
- Exceptions for truly exceptional conditions

---

## Anti-Patterns (THIS PROJECT)

### FORBIDDEN: Never Modify

| Path | Reason |
|------|--------|
| `src/secp256k1/*` | External subtree library |
| `src/leveldb/*` | External subtree library |
| `src/minisketch/*` | External subtree library |
| `src/crc32c/*` | External subtree library |
| `depends/*` | Auto-generated build dependencies |
| `.github/workflows/*` | CI/CD (keep as-is) |

### FORBIDDEN: Never Enable/Use

| Feature | Reason |
|---------|--------|
| **RBF (Replace-By-Fee)** | Disabled in Blackcoin - NEVER enable |
| **Taproot** | `NEVER_ACTIVE` flag set - NEVER enable |
| **Dynamic fees - fee estimation** | Static fee structure only - NEVER port estimation |
| **GUIX** | Incompatible, never adapted |
| **BDB wallet creation** | Deprecated, will be removed (but BDB READ support MUST be preserved) |

### CRITICAL: Never Port During Upgrade

| Bitcoin Core Feature | Blackcoin More Status | Action |
|---------------------|----------------------|--------|
| RBF implementation | Completely disabled | Never port |
| Fee estimation system | Static 100,000 sat/kvB | Never port |
| BDB removal | BDB 6.2 required | Never remove BDB |
| GetAdjustedTime() removal | Required for PoS | Never remove |
| Taproot activation | NEVER_ACTIVE | Never enable |
| Descriptor wallet migration | BDB still supported | Never force migration, but code must be ported from Bitcoin Core |

### FORBIDDEN: Never Port

- Bitcoin Core 30.x+ features — not compatible with Blackcoin's PoS model

### Deprecated RPCs (Use Alternatives)

| Deprecated | Use Instead |
|------------|-------------|
| `getinfo` | Combined RPC calls |
| `signrawtransaction` | `signrawtransactionwithwallet` |
| `addwitnessaddress` | Native segwit addresses |
| `generate` | `generatetoaddress` |
| Accounts API | Label system |

### Code Patterns to Avoid

- `vsnprintf` usage outside `src/dbwrapper.cpp`
- `char` serialization (use `uint8_t`/`int8_t`)
- Direct `uint256` comparisons (forbidden comparison type)
- `QDialog::exec()` (use `QDialog::show()`)

---

## Unique Styles

### Proof-of-Stake Hybrid Architecture

- PoS validation in `src/pos.h`/`src/pos.cpp`
- Legacy PoW code in `src/pow.cpp` (inactive but preserved)
- Hybrid model: evolved from PoW to PoS

### Wallet Deprecation Path

1. Legacy BDB wallets deprecated
2. Migration to descriptor wallets via `migratewallet` RPC
3. BDB support will be removed in future version, but will be preserved for Blackcoin More

### Static Fee Structure

- Unlike Bitcoin's dynamic fees, Blackcoin maintains fixed fees
- Fee rates defined in chain parameters, not market-based

### AI Agent Documentation

- Non-standard `agent/` directory for AI-assisted development
- Contains: `STAKING.md`, `COLD_STAKING.md`, `ANALYSIS.md`

---

## Commands

### Build

**Note:**

- ONLY build when approved by user. agent MUST NOT build without approval.

```bash
./autogen.sh
./configure --enable-tests
make -j$(nproc)

# Specific targets
make blackmored              # Daemon
make blackmore-cli           # CLI
make blackmore-qt            # GUI
make test_blackmore          # Test binary
```

### Test

```bash
make check                           # All tests
src/test/test_blackmore              # C++ unit tests
test/functional/test_runner.py       # Python functional tests
test/functional/test_runner.py --verbose wallet_*.py  # Verbose wallet tests
test/functional/test_runner.py --failfast  # Stop on first failure

# CI scripts
ci/test_run_all.sh                   # All CI test stages
ci/lint_run_all.sh                   # All linting
```

### Lint

```bash
./ci/lint/06_script.sh               # All linting
test/lint/all-lint.py                # Style, formatting
test/lint/check-doc.py               # Documentation
```

### Debug Build

```bash
./configure CFLAGS="-g -O0" --enable-debug
make clean && make
```

### Emergency

```bash
git reset --hard HEAD~1       # Revert last commit
git stash                     # Stash changes
make clean && make distclean  # Full clean rebuild
gdb --args src/test/test_blackmore  # Debug tests
```

### ⚠️ CMake Migration (29.x → 30.x)

**Bitcoin Core 30.x migrated from Autotools to CMake. Blackcoin More will follow.**

| Phase | When | Action |
|-------|------|--------|
| Autotools | 26.x → 29.x | Current build system |
| **CMake Migration** | **29.x → 30.x** | **Migrate per `agent/CMake_MIGRATION.md`** |
| CMake | 30.x+ | New build system |

**Plan**:

1. Complete Bitcoin upgrade to 30.x using Autotools
2. Execute CMake migration (see `agent/CMake_MIGRATION.md` for details)
3. Preserve Blackcoin More differences in CMake configuration:
   - RBF: DISABLED
   - Static fees: 100,000 sat/kvB
   - BDB 6.2: REQUIRED
   - GetAdjustedTime(): PRESERVED
   - nStakeModifier: PRESERVED

```bash
# After completing 29.x → 30.x upgrade:
# Migration will replace these:
rm configure.ac Makefile.am
cmake -B build -S .
cmake --build build
```

---

## Notes

### Testing Reality

- **Functional tests**: Never adapted for Blackcoin's PoS. Expect failures. Manual testing required for staking, transactions, sync.
- **Higher testing effort**: Due to unadapted tests, expect significant manual validation.
- **Pre-release warning**: Current builds are pre-release. Do not use for production staking.

### Security Warnings

- **Wallet encryption**: Lost passphrase = lost funds. Always backup.
- **Scammers**: Console commands can steal funds. Understand before typing.
- **Import risks**: Untrusted files/metadata can cause unexpected issues.
- **Low fees**: Risk of never-confirming transactions.

### Development Hacks (TODO/FIXME)

- `src/validation.cpp`: Witness replacement in packages
- `src/leveldb`: Better compaction implementation
- `src/qt/bitcoinamountfield.h`: CAmount handling quirk
- `src/qt/sendcoinsdialog.cpp`: Replace `QDialog::exec()`

---

## File References

- **Build Config**: `configure.ac` (79KB), `src/Makefile.am`
- **Code Style**: `src/.clang-format`, `src/.clang-tidy`, `.editorconfig`
- **Upgrade Guide**: `UPGRADE.md` (Bitcoin Core porting warnings)
- **Developer Notes**: `doc/developer-notes.md`
- **JSON-RPC API**: `doc/JSON-RPC-interface.md`
- **Build Instructions**: `doc/build-unix.md`, `doc/build-osx.md`, `doc/build-windows.md`

---

*Generated by /init-deep workflow*
