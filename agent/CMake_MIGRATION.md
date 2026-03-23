# Blackcoin More CMake Migration Plan

**Status**: Planned (Phase 4 of Bitcoin 30.x Upgrade)
**Reference**: Bitcoin Core 30.x CMake implementation
**Target**: Migrate from GNU Autotools to CMake

---

## Overview

Bitcoin Core migrated from GNU Autotools to CMake in version 30.x. Blackcoin More must follow this migration to maintain compatibility with the upstream codebase while preserving PoS-specific features.

### Why CMake?

| Factor | Autotools (Current) | CMake (Target) |
|--------|---------------------|----------------|
| Modern Features | Limited | First-class support |
| IDE Integration | Poor | Excellent |
| Cross-compilation | Manual | First-class support |
| Bitcoin Core Sync | Manual porting | Direct sync possible |
| C++20 Support | Requires manual flags | Native support |
| Dependency Management | pkg-config | FetchContent, find_package |

---

## Critical Blackcoin More Differences to Preserve

### Never-Port During Migration

1. **RBF (Replace-By-Fee)**
   - Bitcoin Core: Enabled by default
   - Blackcoin More: COMPLETELY DISABLED
   - Keep: First-seen transaction rule

2. **Fee Estimation**
   - Bitcoin Core: Dynamic fee estimation
   - Blackcoin More: STATIC 100,000 sat/kvB
   - Never port: `fee_estimates.dat` system

3. **BDB Wallet Support**
   - Bitcoin 30.x: Conditionally compiled (`#ifdef USE_BDB`)
   - Blackcoin More: REQUIRED (BDB 6.2)
   - Must preserve: BDB 6.2 wallet storage

4. **GetAdjustedTime()**
   - Bitcoin 28.x+: REMOVED
   - Blackcoin More: REQUIRED for PoS
   - Must preserve: Entire `timedata.cpp/h`

5. **nStakeModifier**
   - Bitcoin 30.x: NOT in `CBlockIndex`
   - Blackcoin More: CRITICAL for PoS kernel
   - Must preserve: `nStakeModifier` field in `CBlockIndex`

6. **PoS Block Header**
   - Bitcoin Core: Standard block header
   - Blackcoin More: Extended header with `nFlags`, `vchBlockSig`
   - Must preserve: Extended header structure

7. **SegWit Threshold**
   - Bitcoin Core: 95%
   - Blackcoin More: 80%
   - Keep: 80% threshold

---

## Migration Phases

### Phase 1: Preparation (Week 1-2)

#### 1.1 Create CMakeLists.txt Structure

```
blackcoin-more/
├── CMakeLists.txt              # Root configuration
├── cmake/
│   ├── module/
│   │   ├── ProcessConfigurations.cmake
│   │   ├── FindBerkeleyDB.cmake
│   │   ├── FindSQLite.cmake
│   │   └── ...
│   ├── platform/
│   │   ├── macOS.cmake
│   │   ├── Linux.cmake
│   │   └── Windows.cmake
│   └── ci/
├── src/
│   ├── CMakeLists.txt          # Source configuration
│   ├── leveldb/CMakeLists.txt  # Keep existing
│   ├── secp256k1/CMakeLists.txt# Keep existing
│   └── ...
└── config/
    └── bitcoin-config.h.cmake  # Config header template
```

#### 1.2 Config Header Migration

**Bitcoin 30.x**: `bitcoin-config.h.cmake` (template-based)
**Blackcoin More**: Must add:

```cmake
# Blackcoin More specific options
option(USE_BDB "Build with Berkeley DB 6.2 wallet support" ON)
mark_as_advanced(USE_BDB)

# Static fee configuration (no dynamic estimation)
set(blackcoin_DEFAULT_MIN_RELAY_TX_FEE 100000)

# PoS configuration
set(blackcoin_N_STAKE_MODIFIER 1)  # Must preserve nStakeModifier field

# GetAdjustedTime() configuration
set(blackcoin_USE_ADJUSTED_TIME 1)  # Required for PoS
```

#### 1.3 Dependency Management

| Dependency | Current (Autotools) | Target (CMake) |
|------------|---------------------|----------------|
| Boost | `PKG_CHECK_MODULES` | `find_package(Boost)` |
| OpenSSL | `PKG_CHECK_MODULES` | `find_package(OpenSSL)` |
| BerkeleyDB 6.2 | Custom check | `FindBerkeleyDB.cmake` |
| Qt5 | `QT5_CHECK` | `find_package(Qt5)` |
| LevelDB | Subtree | Keep as-is |
| Secp256k1 | Subtree | Keep as-is |

### Phase 2: Core Build System (Week 3-4)

#### 2.1 Compiler Flags (Critical)

```cmake
# C++ Standard - UPGRADE from C++17 to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Required for PoS - scrypt SSE2 optimization
# Never remove this - Blackcoin More specific
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 -msse4.1")

# Security flags (from Bitcoin Core)
set(ENABLE_PIE OFF)
set(ENABLE_RELRO ON)
set(ENABLE_STACK_PROTECTOR "strong")
```

#### 2.2 Static Fee Configuration

```cmake
# Blackcoin More uses STATIC fees - never port dynamic estimation
# From src/policy/policy.h
set(blackcoin_DEFAULT_MIN_RELAY_TX_FEE 100000)  # 100k sat/kvB
set(blackcoin_DEFAULT_BLOCK_MIN_TX_FEE 100000)
set(blackcoin_MAX_MONEY 210000000000000000)  # 210 million BLKC
```

#### 2.3 BDB 6.2 Support

```cmake
# FindBerkeleyDB.cmake - Critical for Blackcoin More
find_package(BerkeleyDB 6.2 REQUIRED)
if(BDB_FOUND)
    add_definitions(-DUSE_BDB)
    include_directories(${BDB_INCLUDE_DIR})
    set(BDB_LIBRARY ${BDB_LIBRARIES})
else()
    message(FATAL_ERROR "BerkeleyDB 6.2 is required for Blackcoin More wallet support")
endif()
```

### Phase 3: Source Files (Week 5-6)

#### 3.1 Never-Port Files

```cmake
# These files MUST NOT be ported from Bitcoin 30.x
set(BLACKCOIN_NEVER_PORT
    src/validation.cpp          # Has RBF disabled, GetAdjustedTime
    src/timedata.cpp            # GetAdjustedTime implementation
    src/timedata.h              # GetAdjustedTime declarations
    src/pos.cpp                 # PoS kernel (nStakeModifier)
    src/pos.h                   # PoS kernel declarations
    src/pow.cpp                 # Legacy PoW (scrypt)
    src/crypto/scrypt.cpp       # scrypt SSE2 optimization
)

# These files need significant modification
set(BLACKCOIN_MODIFY
    src/chain.h                 # Add nStakeModifier field
    src/primitives/block.h      # Add nFlags, vchBlockSig
    src/kernel/chainparams.cpp  # Add network ports, static fees
    configure.ac                # Change BDB default to "yes"
)
```

#### 3.2 GetAdjustedTime() Preservation

```cmake
# timedata.cpp must be kept EXACTLY as-is
# It provides GetAdjustedTime() which is REMOVED in Bitcoin 28.x+

# Bitcoin 30.x does NOT have these functions:
# - GetAdjustedTime()
# - GetAdjustedTimeSeconds()
# - NodeClock::now() with adjusted_time_callback

# Blackcoin More MUST preserve:
source_group("timedata" FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/timedata.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/timedata.h
)

# Add to main executable
target_sources(bitcoin-common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/timedata.cpp
)
```

### Phase 4: Binary Targets (Week 7-8)

#### 4.1 Multi-Binary Architecture

```cmake
# Blackcoin More binaries (same as Bitcoin Core)
# Daemon
add_executable(blackmored src/bitcoind.cpp)
target_link_libraries(blackmored bitcoin-common bitcoin-util)

# CLI
add_executable(blackmore-cli src/bitcoin-cli.cpp)
target_link_libraries(blackmore-cli bitcoin-cli)

# Wallet
if(USE_BDB)
    add_executable(blackmore-wallet src/bitcoin-wallet.cpp)
    target_link_libraries(blackmore-wallet bitcoin-wallet)
endif()

# GUI (Qt5)
if(BUILD_GUI)
    add_executable(blackmore-qt src/qt/bitcoin.cpp)
    target_link_libraries(blackmore-qt bitcoin-qt)
endif()

# Utilities
add_executable(blackmore-tx src/bitcoin-tx.cpp)
add_executable(blackmore-chainstate src/bitcoin-chainstate.cpp)
```

### Phase 5: Testing Integration (Week 9-10)

#### 5.1 Unit Tests

```cmake
# CTest configuration
enable_testing()

add_executable(test_blackmore src/test/main.cpp)
target_link_libraries(test_blackmore
    bitcoin-common
    bitcoin-test
    ${Boost_LIBRARIES}
)

add_test(NAME blackmore_unit_tests COMMAND test_blackmore)
```

#### 5.2 Functional Tests

```cmake
# Python functional tests - keep existing test/ directory
# Do NOT port Bitcoin Core's test framework (incompatible with PoS)

# Add custom tests
file(GLOB_RECURSE BLACKCOIN_TESTS "test/functional/*.py")
add_custom_target(functional_tests)
add_custom_command(TARGET functional_tests
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/test/functional/test_runner.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
```

---

## Blackcoin More Specific CMake Configurations

### RBF Configuration

```cmake
# RBF is COMPLETELY DISABLED in Blackcoin More
# Never enable these Bitcoin Core options:
# - OPT_IN_RBF
# - ENABLE_REPLACEMENT

# Verify RBF is disabled
add_definitions(-DCOMPILE_LN_CODE=0)
add_definitions(-DENABLE_REPLACEMENT=0)
```

### Fee Configuration

```cmake
# Blackcoin More uses STATIC fees
# Never port Bitcoin Core's fee estimation:
# - fee_estimates.dat
# - estimateSmartFee()
# - estimatepriority()

set(blackcoin_STATIC_FEE 100000)  # 100,000 sat/kvB
add_definitions(-DDEFAULT_MIN_RELAY_TX_FEE=${blackcoin_STATIC_FEE})
```

### PoS Configuration

```cmake
# Proof-of-Stake specific flags
add_definitions(-DSTAKE_MODIFIER=1)
add_definitions(-DUSE_ADJUSTED_TIME=1)

# nStakeModifier field must be preserved in CBlockIndex
# See src/chain.h - line 220
add_definitions(-DN_STAKE_MODIFIER_PRESENT=1)

# Extended block header fields
add_definitions(-DN_FLAGS_PRESENT=1)
add_definitions(-DVCH_BLOCK_SIG_PRESENT=1)
```

---

## Migration Checklist

### Pre-Migration

- [ ] Backup current Autotools build system
- [ ] Test current build with `make check`
- [ ] Create feature comparison matrix
- [ ] Identify all Blackcoin More-specific patches

### Phase 1: Setup

- [ ] Create root `CMakeLists.txt`
- [ ] Create `cmake/module/` directory
- [ ] Copy Bitcoin 30.x CMake modules
- [ ] Add Blackcoin More-specific options

### Phase 2: Core

- [ ] Migrate compiler flags
- [ ] Configure BDB 6.2 support
- [ ] Set up static fee configuration
- [ ] Configure C++20 standard

### Phase 3: Sources

- [ ] Add source files to CMake
- [ ] Mark never-port files
- [ ] Preserve GetAdjustedTime()
- [ ] Preserve nStakeModifier

### Phase 4: Binaries

- [ ] Create `blackmored` target
- [ ] Create `blackmore-cli` target
- [ ] Create `blackmore-wallet` target (BDB)
- [ ] Create `blackmore-qt` target (optional)

### Phase 5: Testing

- [ ] Enable CTest
- [ ] Add unit test target
- [ ] Integrate functional tests
- [ ] Verify `make check` equivalent

### Post-Migration

- [ ] Remove Autotools files
- [ ] Update CI/CD scripts
- [ ] Update documentation
- [ ] Test build on all platforms

---

## Rollback Plan

If CMake migration causes issues:

```bash
# Keep Autotools files during migration
git stash push -m "Autotools backup before CMake"
# Or preserve in separate branch
git branch autotools-backup
```

---

## References

| Resource | URL |
|----------|-----|
| Bitcoin Core CMake | `/Users/blackcoindev/Development/Blackcoin/bitcoin30/CMakeLists.txt` |
| Bitcoin Core CMake Modules | `/Users/blackcoindev/Development/Blackcoin/bitcoin30/cmake/module/` |
| Current Autotools | `configure.ac`, `Makefile.am` |
| UPGRADE.md | `./UPGRADE.md` |
| Never-Port List | `./AGENTS.md#never-port-list` |

---

## Estimated Timeline

| Phase | Duration | Effort |
|-------|----------|--------|
| Preparation | 2 weeks | Low |
| Core Build System | 2 weeks | Medium |
| Source Files | 2 weeks | High |
| Binary Targets | 2 weeks | Medium |
| Testing | 2 weeks | Medium |
| **Total** | **10-12 weeks** | **High** |

---

*Generated for Blackcoin More Bitcoin 30.x Upgrade Project*
*See UPGRADE.md for complete upgrade plan*
