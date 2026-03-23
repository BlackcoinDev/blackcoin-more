# Changelog

## v27.2.0 (2026-01-24)
**Major upgrade from Bitcoin Core 26.2 to 27.2**

### Core Changes
- Updated to Bitcoin Core 27.2
- Preserved `GetAdjustedTime()` function (removed in Bitcoin 27.x) with compile-time static_assert protection
- Removed RBF (Replace-By-Fee) to maintain first-seen transaction rule
- Removed block pruning to ensure PoS staking access to full chain history
- Support for C++20 compilation

### PoS/Staking
- **Zero-Allocation Descriptor Staking** - Optimized `GetPubKey` path for Descriptor wallets, removing memory allocation in hot loop and reducing block creation time from ~100s to <100ms ([src/wallet/staking.cpp:397-404](src/wallet/staking.cpp#L397))
- **Perfect State StakeCache Sync** - Real-time cache synchronization via `CWallet` event hooks (`AddToSpends`, `blockDisconnected`), ensuring 100% cache accuracy with zero periodic overhead ([src/wallet/staking.cpp:304-309](src/wallet/staking.cpp#L304))
- **Steady-State StakeCache Metrics** - Tracks "steady-state" efficiency (10-minute moving average hit rate) for accurate performance monitoring ([src/wallet/staking.cpp:315-318](src/wallet/staking.cpp#L315))
- **Bech32/Taproot Staking Support** - Full support for staking with Bech32 (native SegWit v0) and Taproot (SegWit v1) UTXOs ([src/wallet/staking.cpp:375-378](src/wallet/staking.cpp#L375))
- **Multi-Wallet Staking Independence** - Per-wallet staking state (no shared `static` timer), eliminating "bullying" bug where frequent stakers starve slower stakers ([src/wallet/wallet.h:746-747](src/wallet/wallet.h#L746))
- **StakeCache Performance Trend Analysis** - 10-minute moving average hit rate and search time history for long-term performance monitoring ([src/wallet/wallet.h:752-756](src/wallet/wallet.h#L752))
- **Cache Flush Reason Tracking** - Tracks reason for cache flushes (size limit, manual, shutdown, cleanup) for debugging ([src/wallet/wallet.h:759-765](src/wallet/wallet.h#L759))
- **StakeCache Time Saved Metrics** - Tracks estimated time saved (ms) by StakeCache (e.g., 1ms per avoided LevelDB hit) ([src/wallet/staking.cpp:360](src/wallet/staking.cpp#L360))
- **Safety Bump Optimization** - Fixed miner to sleep efficiently until next valid window when blocked by MTP, instead of waking every ~1-3 seconds to recheck the same blocked timestamp. Reduces log spam and CPU usage during blocked windows. Pre-calculates next window using `GetAdjustedTimeSeconds()` (matching block validation time reference). Includes modulo 16000 to strip MTP inflation attacks from attackers with +14 second clocks ([src/node/miner.cpp:245-275](src/node/miner.cpp#L245), [src/wallet/wallet.cpp:1540](src/wallet/wallet.cpp#L1540))
- **Skip staketimio After Block Found** - Removed redundant staketimio sleep after successfully finding a PoS block. Previously the miner slept 16-20s then immediately slept again for staketimio ([src/node/miner.cpp:847](src/node/miner.cpp#L847))
- **Fixed `-stakecache` option** - Cache was never used; now properly populates and uses cache for kernel checks (Qtum-derived fix)
- **Stake cache statistics** - New RPC field `stakecache` in `getstakinginfo` (Blackcoin-specific enhancement)
  - Shows: enabled, size, hits, blocks, flushes, efficiency, time saved
  - Stats only shown when `-stakecache=1`
- **Stake cache debug logging** - `-debug=coinstake` shows:
  - Cache HIT with address
  - Cache FLUSH with reason
  - Shutdown stats
- **Miner Diagnostic Patch** - Fixed silent discarding of valid kernels in `node/miner.cpp`. Added detailed logging to identify coinstakes dropped due to 16-second timestamp masking collisions with Median Time Past (MTP). Note: With Safety Bump, ghost blocks should never occur; logging retained for debugging.
- Integrated staking RPC commands into wallet interface
- Fixed `SER_POSMARKER` handling to always include for PoS block headers
- Updated descriptor wallet staking documentation
- RPC: Pass `without_witness=false` to `TxToUniv` in `blockToJSON`

### Network
- Do not apply whitelist permission to onion inbounds
- Fix race condition in self-connect detection
- Prevent sending messages in `NetEventsInterface::InitializeNode`

### Wallet
- Fix `FillPSBT` errantly showing as complete
- Avoid updating `ReserveDestination::nIndex` when `GetReservedDestination` fails

### Build System
- Updated macOS SDK to 15.0
- Configure GCC 12 for ARM cross-compilation
- Fix Qt macOS build with Clang 18
- Fix depends Qt download links
- Fix BDB compilation on OpenBSD
- Fix CXXFLAGS on NetBSD
- Fix build of Qt for 32-bit platforms
- Fix mingw-w64 Qt DEBUG=1 build
- Update Boost download link
- Fetch miniupnpc sources from alternative website
- Update GitHub Actions to latest major versions

### Documentation
- Added comprehensive upgrade documentation (`UPGRADE.md`, `agent/BLOCK_SERIALIZATION.md`, `agent/CMake_MIGRATION.md`)
- Added inline upgrade notes to critical PoS files (`pos.h`, `pos.cpp`, `miner.cpp`, `timedata.h`)
- Added `AGENTS.md` for AI-assisted development
- Added DeepWiki badge to README

### Trivial
- Replace `bitcoind` with `blackmored` in documentation
- Rename `MIN` macro to `_TRACEPOINT_TEST_MIN` in tracing to fix macro redefinition

## v26.2.0 (2024-12-18)
- Begin signalling for SegWit activation on mainnet on June 20, 2025

## v26.2.0-beta-2 (2024-11-20)
- Activated SegWit on testnet on Sep 23, 2024
- Changed miner activation window parameters for BIP9 soft fork deployment
- Updated derivation path with the BIP44 coin type for descriptor wallets
- Abandon coinstake transactions when orphaned (Peercoin commit `f6896a4`)
- Show P2PK addresses for coinstake transactions in RPC
- Show the reward value for coinstake transactions in RPC

## v26.2.0-beta-1 (2024-08-07)
- Updated to Bitcoin Core 26.2
- Activated Segwit on regtest
- New mempool.dat format (backport of Core's PR28207)

## v26.1.0-beta-1 (2024-05-24)
- Updated to Bitcoin Core 26.1
- Create V2 transactions by default
- Disconnect from peers older than version 70015
- Increased `DUST_RELAY_TX_FEE` and `DEFAULT_MIN_RELAY_TX_FEE` to 100000
- Eliminated segfault occurring after a power outage
- Enabled V2 P2P transport by default (backport of Core's PR29347 and 29058)
- Enabled `checkkernel` RPC call
- Only delete the PID file if we created it (backport of Core's PR28946)
- Set minimum UTXO value to be used for staking to 0.1 BLK (can be overridden with `-minstakingamount` parameter)

## v26.0.0-beta-1 (2024-02-12)
- Updated to Bitcoin Core 26.0
- Fixed a bug that prevented adding more inputs in the coinstake transaction for legacy wallets
- Fixed a bug causing inability to connect to fixed seeds
- Fixed reindexing

## v25.1.0-alpha-3 (2024-01-30)
- Set mainnet hard fork date to April 24, 2024
- Use virtual transaction size in minimum fee calculation
- Fixed a bug with header syncing between More 25.1 nodes
- Fixed windows build
- Enabled flushing of orphaned stakes also on wallet start
- Enabled staking with P2WPKH inputs

## v25.1.0-alpha-2 (2023-11-24)
- Fixed a bug with segfault on wallet close when staking is enabled
- Added full support for descriptor wallets, including staking
- Added support for staking with multiple wallets simultaneously
- Removed GUI staking warnings due to incompatibility with multiple staking threads
- Enabled `staking` RPC call
- Multiple minor changes to ThreadStakeMiner() algorithm

## v25.1.0-alpha-1 (2023-10-24)
- Updated to Bitcoin Core 25.1
- Removed OpenSSL
- Implemented maximum witness size policy (Peercoin RFC-0027)
- Added `optimizeutxoset` RPC method to simplify splitting coins for efficient staking (Peercoin PR711)
- Added a GUI warning if unable to stake

## v22.1.0-alpha-2 (2023-01-24)
- Flush orphaned stakes prior to each staking attempt
- Enabled checkpoints by default
- Added rolling checkpoints checks

## v22.1.0-alpha-1 (2023-01-20)
- Updated to Bitcoin Core 22.1

## v13.2.3 (2024-05-18)
- Create V2 transactions by default
- Disconnect from peers older than version 70015
- Increased `DEFAULT_MIN_RELAY_TX_FEE` to 100000

## v13.2.2 (2024-01-24)
- Set mainnet hard fork date to April 24, 2024
- Adjusted minimum fee calculations

## v13.2.1 (2023-07-04)
- Reduced the minimum fee after a fork
- Fixed a bug in the derivation of TxTime that could potentially lead to unplanned hard forks
- Fixed a segfault issue occurring during the initial sync

## v13.2.0 (2022-11-24)
- Changed versioning (backport of Core's PR20223)
- Testnet hard fork: Removed transaction timestamp
- Testnet hard fork: Increased transaction fees and set minimum transaction fee of 0.001 BLK
- Testnet hard fork: Enabled relative timelocks (OP_CHECKSEQUENCEVERIFY, BIP62, 112 and 113)
- Enabled compact block relay protocol (BIP152)
- Added an option to donate the specified percentage of staking rewards to the dev fund (20% by default)
- Set default `MAX_OP_RETURN_RELAY` to 223
- Removed `sendfreetransactions` argument
- Get rid of `AA_EnableHighDpiScaling` warning (backport of Core's PR16254)
- Updated multiple dependencies

## v2.13.2.9 (2022-02-24)
- Updated leveldb, which should resolve the "missing UTXO" staking issue
- Updated dependencies and ported build system from Bitcoin Core 0.20+
- Updated crypto and added CRC32 for ARM64
- Updated univalue to v1.0.3
- Updated to Qt v5.12.11
- Updated to OpenSSL v1.1.1m
- Added `getstakereport` RPC call
- Added `--use-sse2` to enable SSE2
- Code cleanup (headers, names, etc)

## v2.13.2.8 (2021-02-24)
- Immediately ban clients operating on forked chains older than nMaxReorganizationDepth
- Fixed IsDust() policy to allow atomic swaps
- Updated fixed seeds for mainnet and testnet
- Updated dependencies for MacOS

## v2.13.2.7 (2020-11-24)
- Dust mitigation in mempool (by JJ12880 from Radium Core) 
- Compile on MacOS Catalina
- Cross-compile MacOS with Xcode 11.3.1
- Updated dependencies for Windows x64, Linux x64, MacOS, ARM64, ARMv7
- Sign/verify compatibility with legacy clients 
- Increased dbcache to 450MB
- Disabled stake cache for now
- Updated fixed seeds for mainnet and testnet

## v2.13.2.6 (2020-07-21)
- Fix staking memory leak (by JJ12880 from Radium Core)
- Updated fixed seeds
- Added secondary Blackcoin DNS seeder

## v2.13.2.5 (2020-04-28)
- Updated Berkeley DB to 6.2.38
- Updated OpenSSL to 1.0.2u
- Updated fixed seeds
- Changed default port on regtest to 35714

## v2.13.2.4 (2019-11-11)
- Updated fixed seeds
- Added `burn` RPC call
- Set default `MAX_OP_RETURN_RELAY` to 15000
- Removed unit selector from status bar

## v2.13.2.3 (2019-04-02)
- Updated fixed seeds
- Some small fixes and refactorings
- Fixed wrongly displayed balances in GUI and RPC
- Added header spam filter (fake stake vulnerability fix)
- Added total balance in RPC call `getwalletinfo`

## v2.13.2.2 (2019-03-13)
- Updated dependencies
- Updated fixed seeds
- Some small fixes and updates
- Fixed `walletpassphrase` RPC call (wallet now can be unlocked for staking only)
- Allowed connections from peers with protocol version 60016
- Disabled BIP 152

## v2.13.2.1 (2018-12-03)
- Updated to Bitcoin Core 0.13.2
- Some small fixes and updates from Bitcoin Core 0.14.x branch
- Fixed testnet and regtest
- Added Qt 5.9 support for cross-compile
- Added Qt support for ARMv7
- Added out-of-sync modal window (backport of Core's PR8371, PR8802, PR8805, PR8906, PR8985, PR9088, PR9461, PR9462)
- Added support for nested commands and simple value queries in RPC console (backport of Core's PR7783)
- Added `abortrescan` RPC call (backport of Core's PR10208)
- Added `reservebalance` RPC call
- Removed SegWit
- Removed replace-by-fee
- Removed address indexes
- Removed relaying of double-spends
- Removed drivechain support using OP_COUNT_ACKS
- Proof-of-stake related code optimized and refactored

## v2.12.1.1 (2018-10-01)
- Rebranded to Blackcoin More
- Some small fixes and updates from Bitcoin Core 0.13.x branch
- Added "Use available balance" button in send coins dialog (backport of Core's PR11316)
- Added a button to open the config file in a text editor (backport of Core's PR9890)
- Added `uptime` RPC call (backport of Core's PR10400)
- Removed P2P alert system (backport of Core's PR7692)
