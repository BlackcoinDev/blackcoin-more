# Bitcoin 28.4.0 Merge Instructions - Master Summary

**Generated:** March 25, 2026  
**Project:** Blackcoin More → Bitcoin 28.4.0 Upgrade  
**Status:** Category B File Merge Documentation

---

## Overview

This document provides detailed merge instructions for 9 Category B files that require careful manual merge when upgrading Blackcoin More from Bitcoin 27.x → 28.4.0, specifically for Bitcoin Core 28.4.0 integration.

**Important**: Category A files (consensus-critical, like `src/pos.cpp`, `src/pos.h`) will NEVER be merged - these contain Blackcoin-specific PoS logic that must always be preserved.

---

## Merge Priority

| Priority | Files | Risk Level | Reason |
|----------|-------|------------|--------|
| **CRITICAL** | FILE_1 (validation.cpp), FILE_3 (coins.cpp), FILE_7 (core_read.cpp), FILE_9 (node/blockstorage.cpp) | CRITICAL | Consensus-critical PoS logic |
| **HIGH** | FILE_2 (tx_verify.cpp), FILE_8 (rpc/blockchain.cpp) | HIGH | PoS validation & RPC |
| **MEDIUM** | FILE_4 (coinstatsindex.cpp), FILE_5 (wallet_spend.cpp), FILE_6 (wallet_transactions.cpp) | MEDIUM | PoS tracking & wallet RPC |
| **HIGH+** | `net_processing.cpp`, `headerssync.cpp`, `rest.cpp`, `rpc/mining.cpp` | HIGH | SER_POSMARKER & headers-first sync |
| **MEDIUM+** | `miner.cpp`, `wallet/rpc/staking.cpp`, `blockencodings.cpp` | MEDIUM | PoS mining and logic |
| **LOW** | `core_write.cpp`, `net.cpp`, `rpc/server.cpp` | LOW | Logging and minor dependencies |

---

## File-by-File Analysis

### FILE 1: src/validation.cpp

**Risk Level**: HIGH  
**Key Blackcoin Features to Preserve**:
- GetAdjustedTime() wrapper (CRITICAL for PoS)
- CheckBlockSignature() validation
- BCLog::COINSTAKE logging
- fCoinStake + nTime in Coin class

**Merge Strategy**:
- Keep ALL Blackcoin PoS validation logic
- Keep GetAdjustedTime() wrapper at all costs
- Keep CheckBlockSignature() validation
- Copy Bitcoin's performance improvements where non-consensus

**Critical Dependencies**:
- src/primitives/block.h (nFlags, vchBlockSig)
- src/pos.cpp (PoS kernel validation)
- src/consensus/tx_verify.cpp (GetAdjustedTimeSeconds)

---

### FILE 2: src/consensus/tx_verify.cpp

**Risk Level**: HIGH  
**Key Blackcoin Features to Preserve**:
- GetAdjustedTimeSeconds() usage for v2 timestamp
- IsCoinStake() in maturity check
- PoS-specific fee check around IsCoinStake()

**Merge Strategy**:
- Keep GetAdjustedTimeSeconds() calls
- Keep IsCoinStake() check in maturity validation
- Keep PoS fee check
- Copy Bitcoin's C++20 improvements

**Critical Dependencies**:
- src/validation.h (GetAdjustedTimeSeconds declaration)
- src/primitives/transaction.h (IsCoinStake method)

---

### FILE 3: src/coins.cpp

**Risk Level**: HIGH  
**Key Blackcoin Features to Preserve**:
- fCoinStake bitfield in Coin class (CONSENSUS)
- nTime field in Coin class (CONSENSUS)
- Coin constructor with fCoinStake + nTime
- fCoinStake serialization (code = nHeight*4 + coinbase + coinstake*2)

**Merge Strategy**:
- Preserve entire Coin class PoS extension
- KEEP fCoinStake bitfield
- KEEP nTime field
- Preserve serialization logic
- Copy Bitcoin's C++20 optimizations

**Critical Dependencies**:
- src/primitives/transaction.h (IsCoinStake method)
- src/validation.cpp (uses Coin with fCoinStake)

---

### FILE 4: src/index/coinstatsindex.cpp

**Risk Level**: MEDIUM  
**Key Blackcoin Features to Preserve**:
- block.is_pos parameter to GetBlockSubsidy()
- IsCoinStake() in Coin constructor
- nTime field in Coin constructor

---

## 📂 Category B Extended (22 Files)

These files require manual merge due to PoS dependencies. Updated after SER_POSMARKER audit and DataStream migration (March 2026).

| File | PoS Dependency | Instruction | Status |
|------|----------------|-------------|--------|
| `src/net_processing.cpp` | `SER_POSMARKER`, `GetAdjustedTime()` | Preserve `SER_POSMARKER` in header requests. ✅ Uses `SetType()` | Active |
| `src/headerssync.cpp` | `SER_POSMARKER` | Preserve extended header serialization. | Pending |
| `src/node/miner.cpp` | `GetAdjustedTime()`, `nStakeModifier` | Keep PoS coinstake exclusion in block templates. ✅ Local impl active | Active |
| `src/wallet/rpc/staking.cpp`| `SER_POSMARKER`, `IsCoinStake()` | ✅ `SER_DISK \| SER_POSMARKER` for block templates. | Fixed |
| `src/rest.cpp` | `SER_POSMARKER` (blocks only) | ✅ Blocks/headers use `SER_POSMARKER`; filters use `SER_NETWORK` only. | Fixed |
| `src/rpc/mining.cpp` | `SER_POSMARKER`, `IsProofOfStake()` | Preserve `nFlags` in `getblockheader`. | Pending |
| `src/rpc/txoutproof.cpp` | `SER_POSMARKER` | ✅ CMerkleBlock serialization uses `SER_POSMARKER`. | Fixed |
| `src/blockencodings.cpp` | `SER_POSMARKER` | Preserve short ID serialization with `nFlags`. | Pending |
| `src/core_write.cpp` | `nFlags`, `vchBlockSig` | Keep PoS fields in `ValueFromAmount` equivalent. | Pending |
| `src/netmessagemaker.h` | `SER_POSMARKER` | Keep explicit PoS marker injection point. | Pending |
| `src/headerssync.h` | `SER_POSMARKER` | Ensure `CompressedHeader` includes `nFlags`. | Pending |
| `src/net.cpp` | `GetAdjustedTime()` | ✅ Local implementation active. | Active |
| `src/rpc/server.cpp` | BCLog::STAKING | Keep staking-specific RPC logging categories. | Pending |
| `src/util/time.cpp` | `GetAdjustedTime()` | ✅ Local implementation active (Bitcoin removed). | Active |
| `src/validation.h` | `GetAdjustedTime()` | Keep declarations and PoS wrappers. ✅ Local impl | Active |
| `src/consensus/tx_verify.h`| `GetAdjustedTime()` | Keep header declarations. ✅ Local impl | Active |
| `src/wallet/coinselection.cpp`| `GetAdjustedTime()`| ✅ Local implementation active. | Active |
| `src/consensus/tx_check.cpp` | `IsCoinStake()` | Keep coinstake sanity check. Category A | Protected |
| `src/txmempool.cpp` | `GetAdjustedTime()` | ✅ Local implementation active. | Active |
| `src/test/util/setup_common.cpp`| `callback` | Keep PoS time callback for tests. | Pending |
| `src/qt/coincontroldialog.cpp`| `GetAdjustedTime()`| ✅ Local implementation active. | Active |
| `src/wallet/interfaces.cpp` | `IsCoinStake()` | Keep coinstake UI identification. | Pending |
| `src/kernel/chain.cpp` | `IsProofOfStake()` | Preserve block index metadata logic. | Pending |

**Key Changes Made**:
- ✅ `rest.cpp`: Filter endpoints now use `SER_NETWORK` only (filters don't have nFlags)
- ✅ `rpc/txoutproof.cpp`: CMerkleBlock correctly uses `SER_POSMARKER`
- ✅ `wallet/rpc/staking.cpp`: Uses `SER_DISK | SER_POSMARKER` for compatibility
- ✅ `net.cpp`, `util/time.cpp`: Local `GetAdjustedTime()` implementation active
- ✅ `wallet/coinselection.cpp`, `qt/coincontroldialog.cpp`: Using local time implementation
- ✅ `validation.h`, `consensus/tx_verify.h`: Headers reference local implementation

**GetAdjustedTime Status**: ✅ **COMPLETE** — Local implementation is active and working on mainnet. Bitcoin removed it in 28.x, but Blackcoin preserves it permanently for PoS consensus.

**Merge Strategy**:
- Keep PoS extensions to Coin creation
- Keep block.is_pos parameter
- Copy Bitcoin's performance improvements

**Dependencies**:
- src/coins.h (Coin class)
- src/consensus/amount.h (GetBlockSubsidy with is_pos)

---

### FILE 5: src/wallet/rpc/spend.cpp

**Risk Level**: MEDIUM  
**Key Blackcoin Features to Preserve**:
- Staking-only wallet check
- Static fee message (100,000 sat/kvB)
- Burnwallet stake balance checks

**Merge Strategy**:
- Keep staking-only wallet validation
- Keep static fee message
- Keep burnwallet checks
- Copy Bitcoin's improvements

**Dependencies**:
- src/consensus/tx_verify.cpp (GetAdjustedTimeSeconds)
- src/wallet/wallet.h (CWallet with staking flags)

---

### FILE 6: src/wallet/rpc/transactions.cpp

**Risk Level**: MEDIUM  
**Key Blackcoin Features to Preserve**:
- IsCoinStake() check for "generated" flag
- Coinstake transaction handling
- Special code for stake movement

**Merge Strategy**:
- Keep IsCoinStake() checks
- Keep coinstake-specific logic
- Copy Bitcoin's improvements

**Dependencies**:
- src/wallet/wallet.h (CWalletTx with IsCoinStake)
- src/wallet/transaction.h (Transaction class)

---

### FILE 7: src/core_read.cpp

**Risk Level**: HIGH  
**Key Blackcoin Features to Preserve**:
- SER_POSMARKER in block deserialization
- nFlags extraction from header
- vchBlockSig extraction from block

**Merge Strategy**:
- Keep ALL SER_POSMARKER usages
- Keep nFlags field extraction
- Keep vchBlockSig field extraction
- Copy Bitcoin's improvements

**CRITICAL WARNING**: Without SER_POSMARKER, nFlags = 0, PoS blocks fail validation!

**Dependencies**:
- src/primitives/block.h (CBlockHeader with nFlags)
- src/serialize.h (SER_POSMARKER definition)

---

### FILE 8: src/rpc/blockchain.cpp

**Risk Level**: HIGH  
**Key Blackcoin Features to Preserve**:
- SER_POSMARKER in RPC serialization
- nFlags output to RPC
- vchBlockSig output to RPC
- coin.fCoinStake in RPC output

**Merge Strategy**:
- Keep ALL SER_POSMARKER usages in RPC
- Keep nFlags output to RPC
- Keep vchBlockSig output to RPC
- Copy Bitcoin's improvements

**CRITICAL WARNING**: Without SER_POSMARKER, RPC cannot show PoS blocks correctly!

**Dependencies**:
- src/primitives/block.h (CBlockHeader with nFlags)
- src/serialize.h (SER_POSMARKER definition)

---

### FILE 9: src/node/blockstorage.cpp

**Risk Level**: HIGH  
**Key Blackcoin Features to Preserve**:
- nFlags deserialization from disk
- nStakeModifier deserialization from disk
- All PoS extension fields in CDiskBlockIndex

**Merge Strategy**:
- Keep nFlags deserialization from disk
- Keep nStakeModifier deserialization from disk
- Keep CDiskBlockIndex PoS extensions
- Copy Bitcoin's improvements

**CRITICAL WARNING**: Without these, chain cannot sync from disk!

**Dependencies**:
- src/primitives/block.h (Block header)
- src/chain.h (CBlockIndex)

---

## Merge Order Recommendations

### Phase 1: Foundation (Day 1)
1. FILE_3_coins.cpp.md - Coin class PoS extension
2. FILE_9_node_blockstorage.cpp.md - Block storage from disk
3. FILE_7_core_read.cpp.md - Block deserialization

**Why first**: These are foundational - if broken, nothing else will work.

### Phase 2: Validation (Day 2)
4. FILE_1_validation.md - Main validation logic
5. FILE_2_tx_verify.cpp.md - Transaction verification
6. FILE_4_coinstatsindex.cpp.md - Statistics index

**Why second**: These depend on Phase 1 foundation.

### Phase 3: RPC Output (Day 3)
7. FILE_8_rpc_blockchain.cpp.md - Blockchain RPC
8. FILE_5_wallet_spend.cpp.md - Wallet spend RPC
9. FILE_6_wallet_transactions.cpp.md - Wallet transaction RPC

**Why third**: These depend on Phase 1-2 validation.

---

## Common Merge Patterns

### GetAdjustedTime() / GetAdjustedTimeSeconds()

**Pattern**: All PoS code uses GetAdjustedTime() (not GetTime())

**Files affected**: validation.cpp, tx_verify.cpp, spend.cpp, wallet.cpp

**Merge strategy**:
- KEEP all GetAdjustedTime() calls
- DO NOT replace with GetTime()
- This is the #1 most critical PoS feature to preserve

### SER_POSMARKER

**Pattern**: Headers-first sync for PoS blocks

**Files affected**: core_read.cpp, blockchain.cpp, blockencodings.cpp, net_processing.cpp

**Merge strategy**:
- KEEP SER_POSMARKER in ALL block serialization
- Without it, nFlags not serialized → PoS blocks fail validation
- This is the #2 most critical PoS feature to preserve

### IsCoinStake() / fCoinStake

**Pattern**: Distinguishes coinstake from coinbase transactions

**Files affected**: coins.cpp, consensus/tx_verify.cpp, validation.cpp, wallet files

**Merge strategy**:
- KEEP fCoinStake bitfield in Coin class
- KEEP IsCoinStake() method
- Ensure serialization preserves fCoinStake

### nStakeModifier

**Pattern**: PoS kernel hash modifier

**Files affected**: chain.h (CBlockIndex), blockstorage.cpp (loading)

**Merge strategy**:
- KEEP nStakeModifier field in CBlockIndex
- KEEP deserialization from disk
- Never remove - breaks PoS validation

---

## Risk Analysis Summary

| Risk Level | Files | Impact if Broken |
|------------|-------|-----------------|
| CRITICAL | 4 files (1,3,7,9) | Chain cannot validate or sync |
| HIGH | 2 files (2,8) | PoS validation or RPC broken |
| MEDIUM | 3 files (4,5,6) | Staking RPC or statistics broken |

---

## Testing Checklist

After merge, verify:

1. **Chain Sync**:
   - Node can download and validate PoS blocks from peers
   - Node can read PoS blocks from disk
   - No consensus failures on PoS blocks

2. **Staking**:
   - Wallet can create coinstake transactions
   - GetAdjustedTime() used correctly in staking loops
   - Safety bump MTP inflation mitigation working

3. **RPC Output**:
   - getblock shows nFlags field
   - getblockheader shows vchBlockSig
   - listtransactions shows "generated" for coinstake

4. **Validation**:
   - CheckBlockSignature() passes for PoS blocks
   - IsCoinStake() correctly identifies coinstake
   - Fee calculation uses GetAdjustedTimeSeconds()

---

## References

- UPGRADE.md - Complete upgrade plan
- AGENTS.md - Agent knowledge base
- agent/BLOCK_SERIALIZATION.md - PoS block structure
- agent/CHECKKERNEL.md - Kernel validation details
- agent/STAKING.md - Staking implementation
- src/pos.cpp / src/pos.h - PoS kernel implementation

---

**Version**: 1.0  
**Last Updated**: March 25, 2026  
**Target Bitcoin Version**: 28.4.0  
**Blackcoin More Target**: v28.4.0

**Note**: Line numbers in FILE_*.md documents reference pre-merge state. After merge, use function/block identifiers instead of line numbers.
