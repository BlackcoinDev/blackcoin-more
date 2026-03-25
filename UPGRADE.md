# Blackcoin More Upgrade Plan: v26.2.0 → Bitcoin v30.2.0

## Overview

This document outlines the upgrade path from **Blackcoin More v26.2.0** (based on Bitcoin 26.2) to **Bitcoin 30.2.0**. The upgrade involves integrating 4 major Bitcoin Core releases (~1500+ commits) while preserving Blackcoin's PoS consensus and features.

**⚠️ IMPORTANT: PoS Consensus & Soft-Forks**

Blackcoin is a Proof-of-Stake blockchain. Unlike PoW chains where miners can force soft-forks, **any consensus rule change requires stakeholder agreement**.

**Recommended Approach**: Incremental upgrade through 4 phases (26.x → 27.x → 28.x → 29.x → 30.x)

**Current State**: Blackcoin More v27.2.0 (C++20, Bitcoin 27.2 base)  
**Bitcoin 28.4.0 Source**: `/Users/blackcoindev/Development/Blackcoin/bitcoin30/` (commit `b110304705e1e1f97c88c4bb1455ab03c909450a`, Mar 16, 2026)  
**Target State**: Blackcoin More v30.2.0 (C++20, Bitcoin 30.2.0 base)

**Reference Documents:**

- `agent/BLOCK_SERIALIZATION.md` - Complete PoS block structure details
- `agent/CMake_MIGRATION.md` - Build system migration plan
- `AGENTS.md` - Agent knowledge base for AI assistants
- `src/pos.cpp`, `src/pos.h` - PoS kernel implementation

### SegWit Status (March 2026):

| Network | Status | Height | Threshold |
|---------|--------|--------|-----------|
| **Mainnet** | ✅ **ACTIVATED** (March 2026) | 5805000 | **80%** |
| **Testnet** | ✅ ACTIVATED (Sept 2024) | - | **75%** |

---

## ✅ Status Update: v27.2.0 Completed (Jan 2025)

**The upgrade from v26.2.0 to v27.2.0 is COMPLETE.**

### Key Achievements
1. **Core Merge**: Successfully merged Bitcoin Core v27.2.0 changes.
2. **Staking Optimized**: Implemented "Zero-Allocation" staking for Descriptor wallets (see `walkthrough.md`).
3. **Cache Fixed**: Resolved critical bugs in Descriptor cache population and Stake Cache alignment.
4. **Latency Fixed**: Removed 100s block creation stall by eliminating redundant `g_txindex` lookups (disk I/O).
5. **Time Sync Verified**: Confirmed node uses correct network-adjusted time for consensus.
6. **Stability**: hardened `node/miner.cpp` and `wallet/staking.cpp` against regressions.

**Next Step**: Commencing Phase 2 (v28.x) preparation.

---

## 🏗️ Phase 2: v28.x Preparation (COMPLETED ✅)

**Focus**: Preparing for Bitcoin 28.x merge with strict typing and API changes.

### Source Verification (Mar 25, 2026)
- **Bitcoin 28.4.0**: Commit `b110304705e1e1f97c88c4bb1455ab03c909450a` at `/Users/blackcoindev/Development/Blackcoin/bitcoin30/`
- **GenTxid**: Still OLD class (`uint256` + `bool m_is_wtxid`) — matches Blackcoin More exactly. `std::variant` version is **unreleased**.
- **CDataStream**: Removed in Bitcoin 28.x — replaced with `DataStream` (no nType/nVersion fields)

### Completed Preparation ✅
1. **CDataStream → DataStream Migration**: ✅ COMPLETE
   - 17 files modified (+53, -36 lines)
   - 0 production CDataStream uses remaining (42 test/bench uses intentionally preserved)
   - Key fixes: `net.cpp` DataStream constructor issue, `blockencodings.cpp`, `core_read.cpp`, `rpc/blockchain.cpp`, `rpc/mining.cpp`, `wallet/rpc/staking.cpp`
2. **SER_POSMARKER Bug Fixes**: ✅ COMPLETE
   - 7 bugs fixed across 5 files where PoS blocks were missing `nFlags` in serialization
   - Files: `blockencodings.cpp`, `core_read.cpp`, `rpc/blockchain.cpp`, `rpc/mining.cpp`, `wallet/rpc/staking.cpp`
3. **vTxHashes → txns_randomized**: ✅ ALREADY MIGRATED
   - `src/txmempool.h` uses `txns_randomized`
   - `src/kernel/mempool_entry.h` uses `idx_randomized`
   - `src/blockencodings.cpp` uses `txns_randomized`
4. **GenTxid Alignment**: ✅ COMPLETE
   - Blackcoin More GenTxid matches Bitcoin 28.4.0 exactly
   - Old class (`uint256` + `bool m_is_wtxid`) — no `std::variant` in 28.x
5. **Multi-Wallet Staking Independence**: ✅ Complete
   - Per-wallet independent staking timers implemented
   - `static nLastCoinStakeSearchTime` moved to CWallet
6. **Ghost Block Logging**: ✅ Complete
   - MTP collision diagnostics implemented in `node/miner.cpp`
   - Kernel timestamp validation logging

### Remaining for 28.x Merge
1. **Execute 28.x Merge**: Merge Bitcoin 28.4.0 into Blackcoin More
2. **Txid Strict Typing**: Future 29.x+ change — NOT needed for 28.x (28.4.0 still uses `uint256` internally)
3. **GenTxid → std::variant**: Future 29.x+ change — NOT in any released Bitcoin version

---

## ⚠️ Required Reading Before Starting

Before beginning the upgrade process, you **MUST** understand these critical documents:

| Document | Purpose | Status |
|----------|---------|--------|
| [agent/BLOCK_SERIALIZATION.md](agent/BLOCK_SERIALIZATION.md) | PoS block structure, nFlags, vchBlockSig, nStakeModifier | [ ] Read |
| [agent/CMake_MIGRATION.md](agent/CMake_MIGRATION.md) | Build system migration (Autotools → CMake) | [ ] Read |
| [AGENTS.md](AGENTS.md) | AI agent knowledge base for upgrade assistance | [ ] Read |

> [!CAUTION]
> **Critical PoS Fields - NEVER Remove:**
>
> - `CBlockHeader::nFlags` - Marks blocks as PoS/PoW
> - `CBlock::vchBlockSig` - Block signature (DER-encoded ECDSA)
> - `CBlockIndex::nStakeModifier` - PoS kernel hash modifier
> - `SER_POSMARKER` flag - Headers-first sync support
>
> Removing any of these fields **will break consensus** and prevent nodes from syncing.

---

## Phase 1: Analysis & Inventory

### 1.1 Blackcoin-Specific Files (MUST PRESERVE)

#### Core PoS Implementation (PORTED FROM PEERCOIN/QTUM)

| File | Origin | Purpose | Priority |
|------|--------|---------|----------|
| `src/pos.cpp` | Peercoin/Qtum | PoS block validation, stake mining | **CRITICAL** |
| `src/pos.h` | Peercoin/Qtum | PoS declarations | **CRITICAL** |
| `src/validation.cpp` | Qtum | PoS validation logic | **CRITICAL** |
| `src/validation.h` | Qtum | Validation declarations | HIGH |
| `src/node/miner.cpp` | Peercoin/Qtum | PoS block creation, staking | **CRITICAL** |
| `src/node/miner.h` | Peercoin/Qtum | Mining declarations | HIGH |
| `src/wallet/staking.cpp` | Qtum | Wallet staking logic | **CRITICAL** |
| `src/wallet/staking.h` | Qtum | Staking declarations | HIGH |
| `src/wallet/rpc/staking.cpp` | Qtum | Staking RPC methods | **CRITICAL** |
| `src/wallet/rpc/staking.h` | Qtum | Staking RPC declarations | HIGH |
| `src/net_processing.cpp` | Qtum | PoS network handling | HIGH |
| `src/net_processing.h` | Qtum | Network processing declarations | MEDIUM |

**Copyright Headers Confirm Porting:**

- `src/pos.cpp` - Copyright (c) 2011-2013 The PPCoin developers, Copyright (c) 2016-2018 The Qtum developers
- `src/pos.h` - Copyright (c) 2011-2013 The PPCoin developers, Copyright (c) 2016-2018 The Qtum developers
- `src/node/miner.cpp` - Copyright (c) 2020-2022 The Peercoin developers, Copyright (c) 2016-2023 The Qtum developers
- `src/node/miner.h` - Copyright (c) 2020-2022 The Peercoin developers, Copyright (c) 2016-2023 The Qtum developers
- `src/validation.cpp` - Copyright (c) 2016-2018 The Qtum developers
- `src/wallet/staking.cpp` - Copyright (c) 2016-2023 The Qtum developers

#### Chain Parameters

| File | Purpose | Priority |
|------|---------|----------|
| `src/chainparams.cpp` | Blackcoin params, activation heights | **CRITICAL** |
| `src/chainparamsbase.cpp` | Base chain params | HIGH |
| `src/chainparams.h` | Chain params declarations | HIGH |
| `src/chainparamsbase.h` | Base params declarations | MEDIUM |
| `src/chainparamsseeds.h` | DNS seeds | **CRITICAL** |
| `src/kernel/chainparams.cpp` | DevFundAddress, checkpoints | **CRITICAL** |
| `src/kernel/chainparams.h` | Chain params declarations | HIGH |

#### Consensus Changes

| File | Purpose | Priority |
|------|---------|----------|
| `src/consensus/params.h` | Consensus params (nPowTargetSpacing, etc.) | **CRITICAL** |
| `src/consensus/validation.h` | Validation extensions | HIGH |
| `src/validation.cpp` | PoS validation logic | **CRITICAL** |
| `src/validation.h` | Validation declarations | HIGH |

#### Wallet Modifications

| File | Purpose | Priority |
|------|---------|----------|
| `src/wallet/wallet.cpp` | PoS transaction handling | **CRITICAL** |
| `src/wallet/wallet.h` | Wallet with staking | **CRITICAL** |
| `src/wallet/transaction.h` | Transaction with IsCoinStake | HIGH |
| `src/wallet/rpc/spend.cpp` | Spend with coinstake support | HIGH |
| `src/wallet/rpc/staking.cpp` | **Blackcoin-specific RPC calls** | **CRITICAL** |

#### Block/Transaction Extensions

| File | Purpose | Priority |
|------|---------|----------|
| `src/primitives/transaction.h` | IsCoinStake(), IsCoinBase() | **CRITICAL** |
| `src/primitives/block.h` | IsProofOfStake(), IsProofOfWork() | **CRITICAL** |

---

## 2. Blackcoin-Specific Features (READ FIRST!)

### 2.1 PoS-Specific Block Header (CRITICAL)

**Blackcoin has DIFFERENT block serialization than Bitcoin.**

**Reference**: See `agent/BLOCK_SERIALIZATION.md` for complete details.

| Component | Bitcoin Core | Blackcoin More | Action |
|-----------|--------------|----------------|--------|
| `CBlockHeader::nVersion` | 32-bit | 32-bit | YES |
| `CBlockHeader::hashPrevBlock` | uint256 | uint256 | YES |
| `CBlockHeader::hashMerkleRoot` | uint256 | uint256 | YES |
| `CBlockHeader::nTime` | uint32 | uint32 | YES |
| `CBlockHeader::nBits` | uint32 | uint32 | YES |
| `CBlockHeader::nNonce` | uint32 | uint32 | YES |
| `CBlockHeader::nFlags` | **NOT PRESENT** | **uint32 (PoS marker)** | **MUST PRESERVE** |
| `CBlock::vtx` | vector | vector | YES |
| `CBlock::vchBlockSig` | **NOT PRESENT** | **vector (PoS signature)** | **MUST PRESERVE** |
| `CBlockIndex::nStakeModifier` | **NOT PRESENT** | **uint256 (stake modifier)** | **MUST PRESERVE** |
| `SER_POSMARKER` | **NOT PRESENT** | **Serialization flag** | **MUST PRESERVE** |

**Serialization Logic (src/primitives/block.h):**

```cpp
SERIALIZE_METHODS(CBlockHeader, obj)
{
    READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce);

    // CRITICAL: peercoin: do not serialize nFlags when computing hash
    if (!(s.GetType() & SER_GETHASH) && (s.GetType() & SER_POSMARKER))
        READWRITE(obj.nFlags);
}
```

**Key Serialization Rules:**

- `SER_GETHASH` (1 << 2) - For computing hash (excludes nFlags)
- `SER_POSMARKER` (1 << 18) - For sending block headers with PoS marker

**Block Signature (vchBlockSig):**

- PoS blocks MUST have ECDSA signature proving stake ownership
- Signature created by: `key.Sign(block.GetHash(), block.vchBlockSig, 0)`
- Verified by `CheckBlockSignature()` in src/validation.cpp
- Max 65 bytes (compact ECDSA signature)

**Stake Kernel Hash:**

- Uses `nStakeModifier` from CBlockIndex (NOT in Bitcoin Core)
- Formula: `hash(nStakeModifier + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime)`
- Critical for PoS proof calculation (src/pos.cpp:85)

### 2.2 Peercoin/Qtum Ported Features (MUST PRESERVE)

These features were ported from Peercoin and Qtum and are critical to Blackcoin's PoS implementation:

#### Stake Modifier (Peercoin Origin)

**File:** `src/pos.cpp`, `src/chain.h`
**Purpose:** Prevents txout owners from precomputing future proof-of-stake

```cpp
// The stake modifier is calculated from the previous stake modifier and the kernel hash
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel);
```

**Storage:** `CBlockIndex::nStakeModifier` (must be preserved in serialization)

#### Stake Kernel Protocol v3 (Blackcoin/Qtum)

**File:** `src/pos.cpp` - `CheckStakeKernelHash()`
**Formula:** `hash(nStakeModifier + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime) < bnTarget * nWeight`
**Purpose:** Proves ownership of stake UTXO for block signing

#### Stake Cache (Qtum)

**File:** `src/wallet/staking.cpp`, `src/wallet/init.cpp`
**Purpose:** Caches stake weight and UTXO information for better performance
**Config:** `-stakecache=<true/false>` (default: false)

#### CheckBlockSignature (Qtum)

**File:** `src/validation.cpp`
**Purpose:** Verifies PoS block signature using `vchBlockSig`

```cpp
bool CheckBlockSignature(const CBlock& block);
```

#### PoS Network Handling (Qtum)

**File:** `src/net_processing.cpp`
**Purpose:** Handles PoS block propagation and validation over P2P network

#### PoS Block Creation (Peercoin/Qtum)

**File:** `src/node/miner.cpp` - `CreateNewBlock()`
**Purpose:** Creates both PoW and PoS blocks with correct coinstake transaction

### 2.3 GetPoWHash() and Scrypt (Legacy Only)

**File:** `src/primitives/block.cpp`, `src/crypto/scrypt.cpp`

**IMPORTANT**: Scrypt is ONLY for syncing old historical blocks!

```cpp
uint256 CBlockHeader::GetHash() const
{
    // New blocks (nVersion > 6): SHA256 - same as Bitcoin
    if (nVersion > 6)
        return (CHashWriter{} << *this).GetHash();
    
    // Old legacy blocks (nVersion <= 6): Scrypt - for syncing historical data
    return GetPoWHash();
}

uint256 CBlockHeader::GetPoWHash() const
{
    uint256 thash;
    scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));  // Legacy only!
    return thash;
}
```

**SSE2 Optimization:**

- File: `src/crypto/scrypt-sse2.cpp`
- Configure option: `--enable-sse2` in configure.ac
- **Purpose**: Accelerates syncing of old historical testnet blocks
- **NOT used on mainnet** - Mainnet is PoS-only!

### 2.4 Blackcoin-Specific RPC Calls (MUST PRESERVE)

These RPC methods are Blackcoin-specific and MUST be preserved:

| RPC Method | Purpose | File |
|------------|---------|------|
| `getstakinginfo` | Returns staking statistics (enabled, staking, errors, pool size, etc.) | `src/wallet/rpc/staking.cpp` |
| `staking` | Enable/disable wallet staking | `src/wallet/rpc/staking.cpp` |
| `reservebalance` | Reserve balance from staking | `src/wallet/rpc/staking.cpp` |
| `checkkernel` | Check if kernel can stake | `src/wallet/rpc/staking.cpp` |
| `burn` | Burn coins (send to unspendable address) | `src/wallet/rpc/spend.cpp` |
| `burnwallet` | Burn all wallet UTXOs | `src/wallet/rpc/spend.cpp` |
| `optimizeutxoset` | Optimize UTXO set by consolidating inputs | `src/wallet/rpc/spend.cpp` |

### 2.5 Blackcoin-Specific Settings

| Setting | Default | Purpose | File |
|---------|---------|---------|------|
| `-staking` | true | Enable staking | `src/init.cpp` |
| `-staketimio` | 500 | PoS timeout (ms) | `src/wallet/init.cpp` |
| `-stakecache` | false | Enable stake cache | `src/wallet/init.cpp` |
| `-donatetodevfund` | 20 | Dev fund % | `src/wallet/init.cpp` |
| `-txversion` | 2 | Transaction version | `src/wallet/init.cpp` |

### 2.6 Static Fee Structure

Blackcoin uses **STATIC fees**, NOT Bitcoin's dynamic estimation:

| Constant | Value | Purpose |
|----------|-------|---------|
| `DEFAULT_MIN_RELAY_TX_FEE` | 100,000 sat/kvB | Minimum relay threshold |
| `DUST_RELAY_TX_FEE` | 100,000 sat/kvB | Dust limit threshold |
| `GetProofOfStakeSubsidy()` | 1.5 BLK (fixed) | Fixed PoS reward |

**RBF (Replace-By-Fee) is DISABLED in Blackcoin:**

- No `-walletrbf` option
- No `-enable-rbf` option
- No `bumpfee` RPC
- No RBF-related code in wallet initialization

### 2.7 BerkeleyDB 6.2 Required

**CRITICAL**: Existing Blackcoin wallets use BDB 6.2. This MUST be preserved.

| Bitcoin Version | Wallet Database |
|-----------------|-----------------|
| 26.x | BDB 4.8 (deprecated) or SQLite |
| 30.x | Bitcoin removed BDB, Blackcoin MUST keep BDB 6.2 |

**To create new BDB wallets in v30.2.0:**

```bash
# In blackmore.conf
deprecatedrpc=create_bdb
```

### 2.8 Network Configuration

| Network | P2P Port | RPC Port |
|---------|----------|----------|
| Mainnet | 15714 | 15715 |
| Testnet | 25714 | 25715 |
| Regtest | 25714 | 25715 |

**Dev Fund Addresses:**

- Mainnet: `BKDvboD1CzZ5KycP1FRSXRoi7XXhHoQhS1`
- Testnet: `n14L5xqAs7QRzNiTLPNaPeqaF9CRoxzVnU`
- Regtest: empty (no dev fund)

### 2.9 Stake Parameters (Consensus)

These parameters are defined in `src/consensus/params.h` and `src/kernel/chainparams.cpp`:

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `nCoinbaseMaturity` | 500 | UTXO age required before it can stake |
| `nMaxReorganizationDepth` | 500 | Maximum reorg depth |
| `nStakeTimestampMask` | 0xf (15) | Stake timestamp granularity |
| `nTargetSpacing` | 64 seconds | Block spacing |
| `nTargetTimespan` | 24 hours | Difficulty adjustment period |
| `nLastPOWBlock` | Various | Last block where PoW is allowed (testnet only) |

### 2.10 Blackcoin-Specific GUI Quirks

The following GUI files contain Blackcoin-specific modifications that MUST be preserved:

| File | Purpose |
|------|---------|
| `src/qt/bitcoingui.cpp` | Main GUI window, staking status, Blackcoin terminology |
| `src/qt/overviewpage.cpp` | Staking weight display, CanStake() integration |
| `src/qt/walletmodel.cpp` | Stake weight, coinstake tracking |
| `src/qt/walletmodel.h` | getStakeWeight(), CanStake() |
| `src/qt/transactionrecord.cpp` | CoinStake transaction display |
| `src/qt/transactionrecord.h` | IsCoinStake flag |
| `src/qt/transactiontablemodel.cpp` | Coinstake filtering/sorting |
| `src/qt/transactiondesc.cpp` | Coinstake description |
| `src/qt/res/icons/tx_staked.png` | Staked transaction icon |

**GUI Labels and Terminology:**

- "BLK" unit instead of "BTC"
- "Blackcoins" instead of "bitcoins"
- "Stake" terminology
- Blackcoin-specific warnings in passphrase dialog
- Staking weight and status in overview page
- CoinStake transaction icons

---

## 3. Files: Preserve vs Replace vs Remove

### 3.1 Files to PRESERVE (Blackcoin-specific)

| Category | Files |
|----------|-------|
| PoS Core | `src/pos.cpp`, `src/pos.h`, `src/node/miner.cpp`, `src/node/miner.h` |
| Wallet | `src/wallet/staking.cpp`, `src/wallet/staking.h`, `src/wallet/rpc/staking.cpp`, `src/wallet/rpc/staking.h`, `src/wallet/init.cpp` |
| Serialization | `src/primitives/block.h`, `src/primitives/block.cpp`, `src/primitives/transaction.h` |
| Crypto | `src/crypto/scrypt.cpp`, `src/crypto/scrypt-sse2.cpp`, `src/crypto/scrypt.h` |
| Params | `src/kernel/chainparams.cpp`, `src/kernel/chainparams.h`, `src/consensus/params.h`, `src/chainparams.cpp`, `src/chainparams.h`, `src/chainparamsseeds.h` |
| Policy | `src/policy/policy.h` (static fees) |
| Wallet DB | `src/wallet/bdb.cpp`, `src/wallet/db.cpp`, `src/wallet/db.h` |
| Validation | `src/validation.cpp` (PoS logic), `src/validation.h` |

### 3.2 Files to NOT Change (Blackcoin-specific)

| File/Folder | Reason |
|-------------|--------|
| `.github/workflows/build.yml` | Blackcoin CI build configuration |
| `.github/workflows/docker_build_push_26.yml` | Blackcoin Docker build |
| `.gitignore` | Blackcoin-specific ignore rules section |
| `configure.ac` | SSE2 flag for scrypt, BDB 6.2 support |
| `contrib/init/` | Blackcoin-specific init scripts (blackmored, etc.) |
| `src/Makefile.am` | Build configuration for Scrypt/SSE2 |
| `doc/man/*.1` | Blackcoin man pages |

### 3.3 Files to REPLACE from Bitcoin 30.x

| Category | Files |
|----------|-------|
| Networking | `src/net.cpp`, `src/net.h`, `src/net_processing.cpp` (most) |
| Mempool | `src/txmempool.cpp`, `src/txmempool.h` (core logic) |
| Validation | `src/validation.cpp`, `src/validation.h` (core logic, adapt PoS) |
| Consensus | `src/consensus/*` (adapt params) |
| RPC | Most of `src/rpc/*` (adapt Blackcoin RPCs) |
| Wallet | `src/wallet/*` (adapt, not replace) |
| GUI | `src/qt/*` (adapt Blackcoin GUI) |

### 3.4 External Subtrees (UPDATE NORMALLY)

| Directory | Action |
|-----------|--------|
| `src/secp256k1/` | Pull latest from Bitcoin 30.x |
| `src/leveldb/` | Pull latest from Bitcoin 30.x |
| `src/minisketch/` | Pull latest from Bitcoin 30.x |
| `src/crc32c/` | Pull latest from Bitcoin 30.x |

### 3.5 Items NOT to Port from Bitcoin 30.x

These Bitcoin 30.x features must NOT be ported:

| Feature | Reason |
|---------|--------|
| RBF (`-walletrbf`, `-enable-rbf`) | RBF is DISABLED in Blackcoin |
| Fee estimation (`src/kernel/feerate*.cpp`) | Blackcoin uses STATIC fees |
| Dynamic fee calculation | Blackcoin has hardcoded fee constants |
| Taproot activation | Taproot is coded but NEVER_ACTIVE |
| GUIX | Never tested or adapted for Blackcoin |

**Note**: Functional tests (`test/functional/*`) were never adapted for Blackcoin's PoS implementation. They may fail or be incompatible. Do not attempt to fix them as part of this upgrade.

---

## 4. Incremental Upgrade Strategy

### 4.1 Why Incremental?

| Risk | Impact |
|------|--------|
| **API churn** | Txid/Wtxid types, COutPoint changes, mempool API changes |
| **PoS preservation** | Each version may break consensus-adjacent code |
| **Testing complexity** | Functional tests never adapted for PoS |

**Direct 26.x → 30.x merge is TOO RISKY.**

### 4.2 Upgrade Path

```
Blackcoin More v26.2.0
        ↓
    Phase 1: Bitcoin 27.x [GetAdjustedTime() removed]
        ↓
    Phase 2: Bitcoin 28.x [Txid/Wtxid types, CDataStream→DataStream]
        ↓
    Phase 3: Bitcoin 29.x [GenTxid→std::variant]
        ↓
    Phase 4: Bitcoin 30.x [uint256→Txid, BDB removal, CMake migration]
        ↓
    Blackcoin More v30.2.0
```

### 4.3 Phase Details

### 4.3 Detailed Phase Roadmap

#### ✅ Phase 1: v27.x Compliance (Completed)
*   **Step 1.1**: Identify `GetAdjustedTime()` removal risk.
*   **Step 1.2**: Verified `timedata.cpp` preservation and PoS calls.
*   **Step 1.3**: Annotated critical call sites with `// BLACKCOIN-SPECIFIC`.

#### 🏗️ Phase 2: v28.x Preparation (v2.7 Planning COMPLETE ✅)
*   **Step 2.1**: **`CDataStream` → `DataStream` Rename**. ✅ COMPLETE (17 files, 0 production uses remaining)
*   **Step 2.2**: **Strict `Txid` Typing**. ⏳ NOT YET NEEDED — Bitcoin 28.4.0 still uses `uint256` internally. This is a 29.x+ change.
*   **Step 2.3**: **PoS Field Verification**.
    *   ✅ **Action**: `nStakeModifier` (in `CBlockIndex`) and `IsCoinStake` (in `Coin`) verified untouched.
    *   ✅ **Action**: SER_POSMARKER bugs fixed (7 bugs in 5 files).
    *   ✅ **Action**: GenTxid verified matching Bitcoin 28.4.0 exactly.
*   **Step 2.4**: **`GetAdjustedTime()` Restoration Strategy**.
    *   ✅ **Fact**: Removed in Bitcoin 28.0 (PR #28849).
    *   ✅ **Action**: Restore locally in `src/util/time.cpp` to maintain PoS consensus.
*   **Step 2.5**: *Merge v28.x Upstream*. ⏳ PENDING — Ready for Wave 0 execution.

#### 🔁 Phase 3: v29.x Preparation (GenTxid Refactor)
*   **Step 3.1**: **Refactor `GenTxid`**. (Move from custom class to `std::variant`-compatible structure).
*   **Step 3.2**: **Mempool Modernization**. (Update `txmempool` to use `txns_randomized`).
*   **Step 3.3**: *Merge v29.x Upstream*.

#### 🚀 Phase 4: v30.x Finalization (Cleanup)
*   **Step 4.1**: **Final `uint256` Cleanup**. (Remove strict typing bridges).
*   **Step 4.2**: **BDB 6.2 Rescue**. (Ensure wallet database support is preserved).
*   **Step 4.3**: **CMake Migration**. (Switch build system from Autotools to CMake).
*   **Step 4.4**: *Merge v30.x Upstream*.

### 4.4 Effort Estimate

| Phase | Duration |
|-------|----------|
| Phase 1 (27.x) | 1-2 weeks |
| Phase 2 (28.x) | 2-4 weeks |
| Phase 3 (29.x) | 1-2 weeks |
| Phase 4 (30.x) | 2-3 weeks |
| **Total** | **6-11 weeks** |

---

## 5. API Changes (Reference)

### 5.1 Type Changes by Bitcoin Version

| Bitcoin Version | Change | Blackcoin Status |
|-----------------|--------|-----------------|
| 27.x/28.x | `GetAdjustedTime()` removed | ✅ MANDATORY LOCAL RESTORATION (`util/time.cpp`) |
| 28.x | `CDataStream` → `DataStream` | ✅ COMPLETE (0 production uses) |
| 28.x | `GenTxid` class (old: `uint256` + `bool`) | ✅ MATCHES 28.4.0 exactly |
| 28.x | `vTxHashes` → `txns_randomized` | ✅ ALREADY MIGRATED |
| 29.x | `GenTxid` → `std::variant<Txid, Wtxid>` | ⏳ NOT IN ANY RELEASED VERSION (unreleased) |
| 30.x | `uint256` → `Txid` final | ⏳ NOT YET NEEDED (28.4.0 still uses `uint256`) |

### 5.2 Function Changes

| Bitcoin 26.x | Bitcoin 30.x |
|--------------|--------------|
| `IsCoinBase()` | (unchanged) |
| `IsCoinStake()` | ADD THIS |
| `IsProofOfStake()` | ADD THIS |
| `GetAdjustedTime()` | REMOVED - preserve if needed |
| `pool.queryHashes(vtxid)` | `for (const CTxMemPoolEntry& e : pool.entryAll())` |

### 5.3 Add to CTransaction

```cpp
// In src/primitives/transaction.h
bool IsCoinStake() const {
    return (vin.size() > 0 && (!vin[0].prevout.IsNull()) 
            && vout.size() >= 2 && vout[0].IsEmpty());
}
```

### 5.4 Add to CTxOut

```cpp
// In src/primitives/transaction.h
bool IsEmpty() const {
    return (nValue == 0 && scriptPubKey.empty());
}
```

### 5.5 C++ Requirements

- **C++20** required
- **Clang 15+** or **GCC 12+**
- **Boost 1.77.0+**
- **Python 3.9+** for tests

---

## 6. Build System Migration (Autotools → CMake)

### 6.1 Why Migrate to CMake?

Bitcoin Core 30.x migrated from GNU Autotools to CMake. Blackcoin More currently uses Autotools.

| Aspect | Autotools (Current) | CMake (Bitcoin 30.x) |
|--------|---------------------|----------------------|
| Build Files | configure.ac, Makefile.am | CMakeLists.txt |
| Configuration | `./configure --enable-foo` | `cmake -DFOO=ON` |
| Bitcoin Core Sync | Manual cherry-pick | Direct merge possible |

### 6.2 CMake Migration Plan (Phase 4)

**CMake Migration occurs in Phase 4 (29.x → 30.x):**

1. Complete Bitcoin upgrade to 30.x using Autotools
2. Execute CMake migration per `agent/CMake_MIGRATION.md`
3. Preserve Blackcoin More differences in CMake:
   - RBF: DISABLED
   - Static fees: 100,000 sat/kvB
   - BDB 6.2: REQUIRED
   - GetAdjustedTime(): PRESERVED
   - nStakeModifier: PRESERVED

| Build System | Versions | Status |
|--------------|----------|--------|
| Autotools | 26.x → 29.x | Current |
| CMake | 30.x+ | Migrate in Phase 4 |

### 6.3 Blackcoin-Specific CMake Options

```cmake
# REQUIRED: BDB 6.2 support (Bitcoin 30.x removed this)
cmake_dependent_option(USE_BDB "Enable BDB 6.2 wallet support." ON "ENABLE_WALLET" OFF)

# REQUIRED: Keep RBF disabled (Bitcoin 30.x has RBF enabled)
option(RBF_ENABLED "Enable Replace-By-Fee (MUST remain OFF)." OFF)

# REQUIRED: Keep static fees (Bitcoin 30.x has dynamic estimation)
option(DYNAMIC_FEES "Enable dynamic fee estimation (MUST remain OFF)." OFF)

# Blackcoin More uses SSE2 for scrypt (not in Bitcoin)
option(USE_SSE2 "Enable SSE2 for scrypt library." OFF)
```

### 6.4 Critical: nStakeModifier Preservation

**Bitcoin 30.x does NOT have `nStakeModifier` in CBlockIndex!** This field is CRITICAL for PoS:

```cpp
// src/chain.h - MUST PRESERVE THIS FIELD
struct CBlockIndex {
    // ...
    uint256 nStakeModifier{};  // CRITICAL: Not in Bitcoin Core
    // ...
};
```

Used in `src/pos.cpp:CheckStakeKernelHash()` for stake proof validation.

---

## 7. Testing Strategy

### 7.1 Testnet Testing (FIRST)

**SegWit is ALREADY ACTIVATED on testnet** (Sept 2024). Testnet supports **both PoW mining AND PoS staking**.

| Phase | Test | Description |
|-------|------|-------------|
| 1 | Sync from scratch | Download testnet blockchain |
| 2 | Mining (PoW) | Test PoW block production (reward: 10000 BLK, uses SHA256 for new blocks) |
| 3 | Staking (PoS) | Test stake generation (SegWit active) |
| 4 | Transactions | Send regular and coinstake transactions |

**Note**: Scrypt is ONLY for syncing **old historical testnet blocks**. New testnet blocks use **SHA256** (same as Bitcoin). SSE2 accelerates sync of old blocks.

### 7.2 Mainnet Testing (AFTER TESTNET)

**Mainnet is PoS-only - NO mining allowed!**

| Phase | Test | Description |
|-------|------|-------------|
| 1 | Sync from scratch | Download mainnet blockchain |
| 2 | Staking (PoS) | Test stake generation (CRITICAL - mainnet is PoS-only) |
| 3 | Transactions | Send transactions |
| 4 | SegWit rules | Monitor BIP-9 voting (80% threshold, activation: June 20, 2025) |

### 7.3 Required Manual Tests

```bash
# PoS tests
blackmore-cli getstakinginfo
blackmore-cli staking true
blackmore-cli checkkernel "<txid>" <output_index>
blackmore-cli reservebalance 0

# Wallet tests
blackmore-cli listwallets
blackmore-cli getwalletinfo
blackmore-cli getbalance

# Transaction tests
blackmore-cli sendtoaddress "<addr>" 0.1
blackmore-cli burn 0.1
blackmore-cli burnwallet
blackmore-cli optimizeutxoset
```

### 7.4 Critical Tests

| Test | Purpose | Status |
|------|---------|--------|
| `getstakinginfo` RPC | Returns staking stats | REQUIRED |
| `staking` RPC | Enable/disable wallet staking | REQUIRED |
| `reservebalance` RPC | Reserve balance from staking | REQUIRED |
| `checkkernel` RPC | Check if kernel can stake | REQUIRED |
| `burn` RPC | Burn coins | REQUIRED |
| `burnwallet` RPC | Burn all wallet UTXOs | REQUIRED |
| `optimizeutxoset` RPC | Optimize UTXO set | REQUIRED |
| Manual staking test | Stake on regtest | REQUIRED |
| Manual sync test | Sync from scratch | REQUIRED |
| Manual tx test | Send transactions | REQUIRED |

**Note**: Functional tests were never adapted for Blackcoin. Do not expect them to pass. Focus on manual testing of core staking and transaction functionality.

---

## 8. Rollback Plan

If a phase fails:

1. **Identify failure point**: Build, PoS, or transactions
2. **Preserve working state**: Commit or stash
3. **Analyze root cause**: Check API changes affecting PoS
4. **Fix or rollback**: Either fix the issue or rollback to previous version
5. **Retry**: Once fixed, retry the phase

**Escape hatch**: If incremental approach fails repeatedly, consider staying on the last working Bitcoin version.

---

## 9. Pre-Merge Checklist

- [ ] Backup current v26.2.0 branch
- [ ] Fetch Bitcoin 30.2.0
- [ ] Create file inventory of Blackcoin-specific code
- [ ] Document all API changes between 26.x and 30.x
- [ ] **Port BDB 6.2 support** from Blackcoin to Bitcoin 30.x (Bitcoin removed BDB!)
- [ ] Add `deprecatedrpc=create_bdb` to allow BDB wallet creation
- [ ] **DO NOT** modify CI/CD workflows (build.yml, docker_build_push_26.yml)
- [ ] **DO NOT** modify GUIX (never adapted for Blackcoin)
- [ ] Create test environment

---

## 10. GetAdjustedTime() Restoration Strategy (Phase 2: MANDATORY)

**⚠️ WARNING**: Bitcoin 28.0 removed `GetAdjustedTime()` and the `timedata` module (PR #28849). This logic is CRITICAL for PoS kernel validation and MUST be restored locally.

### Affected Files (50+ locations)

| File | Usage | Priority |
|------|-------|----------|
| `src/init.cpp:1482` | `adjusted_time_callback = GetAdjustedTime` | **CRITICAL** |
| `src/node/miner.cpp:68,179,221,272,324` | PoS block creation | **CRITICAL** |
| `src/pos.h:53-80` | PoS kernel validation | **CRITICAL** |
| `src/wallet/coinselection.cpp:416-418` | Fee calculations | HIGH |
| `src/wallet/spend.cpp:1007-1010` | Transaction creation | HIGH |
| `src/wallet/rpc/staking.cpp:278-280` | Staking RPC | **CRITICAL** |
| `src/wallet/rpc/spend.cpp:447-449,1409-1455` | Spend RPCs | HIGH |
| `src/rpc/mining.cpp:398-400,637` | Mining RPCs | HIGH |
| `src/rpc/blockchain.cpp:9` | Blockchain RPCs | MEDIUM |
| `src/txmempool.cpp:10` | Mempool | MEDIUM |

### Migration Steps

1. **DO NOT** remove `src/timedata.cpp` or `src/timedata.h`
2. **DO NOT** accept Bitcoin's removal of these functions
3. If Bitcoin replaces with `NodeClock::now()`, preserve wrapper:

   ```cpp
   // src/timedata.h - Blackcoin More MUST preserve
   NodeClock::time_point GetAdjustedTime();
   int64_t GetAdjustedTimeSeconds();
   ```

4. After merge, verify with:

   ```bash
   grep -r "GetAdjustedTime" src/ | wc -l  # Should be 50+
   ```

### Compile-Time Verification

Add to `src/pos.cpp`:

```cpp
// Ensure GetAdjustedTime exists - compile fails if removed
static_assert(std::is_invocable_v<decltype(GetAdjustedTime)>, 
              "GetAdjustedTime MUST exist for PoS");
```

---

## 11. CDataStream → DataStream Migration (Phase 2)

**Status**: ✅ COMPLETE

Bitcoin 28.x removed `CDataStream` and replaced it with `DataStream` (which has no nType/nVersion fields). This migration is complete.

### Migration Results

| Metric | Value |
|--------|-------|
| Files modified | 17 |
| Lines added | +53 |
| Lines removed | -36 |
| Production CDataStream remaining | **0** |
| Test/bench CDataStream preserved | 42 (intentionally) |

### Files Modified
- `src/bitcoin-util.cpp`, `src/external_signer.cpp`, `src/psbt.cpp`
- `src/rest.cpp` (10 locations), `src/rpc/txoutproof.cpp` (2 locations)
- `src/qt/psbtoperationsdialog.cpp`, `src/qt/sendcoinsdialog.cpp`
- `src/wallet/rpc/spend.cpp` (3 locations)
- `src/wallet/db.h`, `src/wallet/walletdb.cpp` (SER_DISK preserved)
- `src/wallet/rpc/staking.cpp`, `src/blockencodings.cpp` (SER_POSMARKER preserved)
- `src/core_read.cpp`, `src/rpc/blockchain.cpp`, `src/rpc/mining.cpp`
- `src/net.cpp` (DataStream constructor fix)

### Key Fixes Applied
1. **`net.cpp`**: Fixed DataStream constructor issue — `CNetMessage` takes `DataStream&&` but DataStream lacks `int` constructor
2. **PoS serialization**: SER_POSMARKER preserved in all PoS-relevant files
3. **SER_DISK preservation**: Wallet database files use SER_DISK flag

### Verification
```bash
# Production CDataStream count (expect 0)
grep -rn "CDataStream" src/ --include="*.cpp" --include="*.h" | grep -v "test/" | grep -v "bench/" | wc -l
# Output: 0

# SER_POSMARKER count (expect ~130)
grep -rn "SER_POSMARKER" src/ | wc -l
# Output: ~130
```

---

## 12. Coin Class Extension (CRITICAL for PoS)

Blackcoin extends the `Coin` class with `IsCoinStake()` - this is NOT in Bitcoin Core.

### Required Modification in src/coins.h

```cpp
class Coin {
    // ... Bitcoin fields ...
    
    // ⚠️ BLACKCOIN MORE ONLY - NOT IN BITCOIN
    bool fCoinStake;  // True if this coin is from a coinstake transaction
    
    bool IsCoinStake() const {
        return fCoinStake;
    }
    
    // Constructor must include fCoinStake
    Coin(CTxOut&& outIn, int nHeightIn, bool fCoinBaseIn, bool fCoinStakeIn, int nTimeIn)
        : out(std::move(outIn)), nHeight(nHeightIn), fCoinBase(fCoinBaseIn), 
          fCoinStake(fCoinStakeIn), nTime(nTimeIn) {}
};
```

### Files Using IsCoinStake() (40+ locations)

- `src/validation.cpp` - Block validation
- `src/consensus/tx_verify.cpp` - Maturity checks
- `src/wallet/wallet.cpp` - Wallet operations
- `src/wallet/rpc/transactions.cpp` - Transaction display
- `src/coins.cpp` - Coin cache operations

### Verification After Merge

```bash
grep -r "IsCoinStake" src/ | wc -l  # Should be 40+
```

### CTransaction::IsCoinStake() (Also CRITICAL)

The `CTransaction` class also requires `IsCoinStake()` - this identifies coinstake transactions:

```cpp
// In src/primitives/transaction.h - REQUIRED for PoS
// A coinstake transaction has:
// - At least 1 input (the staking coin)
// - At least 2 outputs (empty marker + reward)
// - First output is empty (marker)
bool IsCoinStake() const {
    return vin.size() > 0 && vout.size() >= 2 && vout[0].IsEmpty();
}
```

**Used in:**

- `CBlock::IsProofOfStake()` - Checks if `vtx[1]->IsCoinStake()`
- `CheckProofOfStake()` - Validates coinstake transaction
- `CheckBlock()` - Validates block structure
- Wallet staking operations

---

## 13. Regtest Testing Instructions

Regtest is the fastest way to verify PoS changes work correctly.

### Quick Start

```bash
# Start regtest daemon
./blackmored -regtest -daemon

# Generate initial PoW blocks (need coins to stake)
./blackmore-cli -regtest generatetoaddress 101 $(./blackmore-cli -regtest getnewaddress)

# Check balance
./blackmore-cli -regtest getbalance

# Enable staking
./blackmore-cli -regtest staking true

# Check staking status
./blackmore-cli -regtest getstakinginfo
```

### Expected Output

```json
{
  "enabled": true,
  "staking": true,
  "errors": "",
  "currentblockweight": 4000,
  "currentblocktx": 0,
  "pooledtx": 0,
  "difficulty": 0.00024414,
  "search-interval": 16,
  "weight": 101000000000,
  "netstakeweight": 101000000000,
  "expectedtime": 64
}
```

### Critical Regtest Tests

| Test | Command | Expected |
|------|---------|----------|
| Staking enabled | `getstakinginfo` | `"staking": true` |
| Block produced | Wait ~60s, then `getblockcount` | Count increased |
| Block is PoS | `getblock $(getbestblockhash)` | `"flags": "proof-of-stake"` |
| Signature valid | `getblock $(getbestblockhash)` | `"signature"` field present |
| nStakeModifier | `getblock $(getbestblockhash)` | `"modifier"` field present |

### After Each Phase

Run these tests immediately after merging each phase:

```bash
# Phase verification script
./blackmore-cli -regtest getstakinginfo | grep staking
./blackmore-cli -regtest staking true
sleep 120  # Wait for stake
./blackmore-cli -regtest getblock $(./blackmore-cli -regtest getbestblockhash) | grep flags
```

---

## 14. Merge Conflict Resolution Strategy

### High-Conflict Files

| File | Size | Blackcoin Changes | Strategy |
|------|------|-------------------|----------|
| `src/validation.cpp` | 294KB | PoS validation, GetAdjustedTime | **Careful merge** |
| `src/net_processing.cpp` | 298KB | PoS block handling | **Careful merge** |
| `src/wallet/wallet.cpp` | Large | Staking, IsCoinStake | **Careful merge** |
| `src/primitives/block.h` | Small | nFlags, vchBlockSig | **Keep Blackcoin** |
| `src/chain.h` | Medium | nStakeModifier | **Keep Blackcoin** |

### Pre-Merge Documentation

Before each phase, document Blackcoin-specific changes:

```bash
# Create inventory of Blackcoin changes
git log --oneline --all | grep -i "blackcoin\|pos\|stake" > blackcoin_commits.txt

# Document specific lines in critical files
git blame src/validation.cpp | grep -E "(pos|stake|Blackcoin|GetAdjusted)" > validation_blackcoin.txt
git blame src/chain.h | grep -E "(pos|stake|Modifier)" > chain_blackcoin.txt
```

### Conflict Resolution Rules

1. **Always Keep Blackcoin Code For**:
   - `nStakeModifier` field and usage
   - `GetAdjustedTime()` calls
   - `CheckBlockSignature()` function
   - `vchBlockSig` field
   - `nFlags` field
   - `IsCoinStake()` checks
   - `SER_POSMARKER` flag

2. **Accept Bitcoin Code For** (if no PoS impact):
   - Networking improvements
   - Mempool optimizations (verify no PoS impact)
   - RPC improvements (add Blackcoin RPCs back)
   - GUI improvements (preserve staking UI)

3. **Merge Carefully**:
   - `validation.cpp` - Keep all CheckProofOfStake logic
   - `net_processing.cpp` - Keep PoS block handling
   - `wallet.cpp` - Keep staking integration

---

## 15. Pre-Release QA Checklist

### Build Verification

- [ ] Compiles on Linux (GCC 12+ / Clang 15+)
- [ ] Compiles on macOS (Clang 15+)
- [ ] Compiles on Windows (MSVC 2022)
- [ ] All unit tests pass (`make check`)
- [ ] No compiler warnings in PoS code

### Sync Verification

- [ ] Testnet syncs from genesis (full sync)
- [ ] Mainnet syncs from genesis (full sync)
- [ ] Mainnet syncs from recent snapshot
- [ ] Old blocks with scrypt hash correctly (testnet legacy)

### Staking Verification (CRITICAL)

- [ ] `getstakinginfo` returns valid data
- [ ] `staking true` enables staking without errors
- [ ] Regtest produces PoS block within 5 minutes
- [ ] Block signature is valid (`CheckBlockSignature()` passes)
- [ ] `nStakeModifier` computed correctly
- [ ] Coinstake transaction is valid
- [ ] Stake reward is 1.5 BLK

### Wallet Verification

- [ ] Existing BDB wallet opens correctly
- [ ] SQLite wallet opens correctly
- [ ] New BDB wallet creation works (with `deprecatedrpc=create_bdb`)
- [ ] New SQLite wallet creation works
- [ ] Send transaction succeeds
- [ ] Receive transaction displays correctly
- [ ] Coinstake transaction displays with stake icon

### RPC Verification

| RPC | Test | Status |
|-----|------|--------|
| `getstakinginfo` | Returns staking stats | [ ] |
| `staking true/false` | Enables/disables staking | [ ] |
| `reservebalance 100` | Reserves balance from staking | [ ] |
| `checkkernel <txid> <n>` | Checks if can stake | [ ] |
| `burn 0.1` | Burns coins successfully | [ ] |
| `burnwallet` | Burns all UTXOs | [ ] |
| `optimizeutxoset` | Consolidates UTXOs | [ ] |

### Network Verification

- [ ] Node connects to testnet peers
- [ ] Node connects to mainnet peers
- [ ] Blocks propagate correctly
- [ ] Transactions propagate correctly
- [ ] No fork from main chain

### Final Checks

- [ ] Version string correct (`blackmore-cli --version`)
- [ ] Help text shows Blackcoin branding
- [ ] GUI shows Blackcoin branding and staking status
- [ ] CHANGELOG.md updated
- [ ] Release notes prepared

---

## 16. Example blackmore.conf

```bash
# Blackcoin-specific settings
staking=true             # Enable staking by default
staketimio=500           # PoS timeout in milliseconds (default)
stakecache=false         # Staking cache disabled by default (saves memory)
donatetodevfund=20       # Donate 20% of staking rewards to dev fund (default)
txversion=2              # Transaction version (default: 2)
deprecatedrpc=create_bdb # Allow creating new BDB wallets

# RBF is DISABLED - DO NOT enable
# Fee structure is STATIC - DO NOT change to Bitcoin's dynamic fees
# Taproot is coded but NEVER_ACTIVE - DO NOT activate
```

---

## 17. References

- Bitcoin Core Release Notes: <https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes.md>
- Bitcoin Core Git History: <https://github.com/bitcoin/bitcoin/commits/master>
- Blackcoin More Repository: <https://github.com/BlackcoinDev/blackcoin-more>
- agent/BLOCK_SERIALIZATION.md - Complete PoS block structure
- agent/CMake_MIGRATION.md - Build system migration plan
- AGENTS.md - Agent knowledge base
- src/pos.cpp - PoS kernel implementation
- src/pos.h - PoS declarations
- src/primitives/block.h - Block structure with nFlags, vchBlockSig
- src/chain.h - CBlockIndex with nStakeModifier

---

*Last Updated: March 25, 2026*
*Version: 4.0 (Phase 2 v28.x preparation COMPLETE — CDataStream migration, SER_POSMARKER fixes, GenTxid verified vs Bitcoin 28.4.0)*
