# Blackcoin More Upgrade Plan: v26.2.0 → Bitcoin v30.2.0

## Overview

This document outlines the upgrade path from Blackcoin More 26.2.0 (based on Bitcoin 26.2) to Bitcoin 30.2.0. The upgrade involves integrating 4 major Bitcoin Core releases (~1500+ commits) while preserving Blackcoin's PoS consensus and features.

**Target**: Bitcoin 30.2.0
**Source**: Blackcoin More 26.2.0 (commit: `b0ed2edc55`)
**Scope**: Full protocol upgrade with PoS preservation

---

## Phase 1: Analysis & Inventory

### 1.1 Blackcoin-Specific Files (MUST PRESERVE)

#### Core PoS Implementation
| File | Purpose | Priority |
|------|---------|----------|
| `src/pos.cpp` | PoS block validation, stake mining | CRITICAL |
| `src/pos.h` | PoS declarations | CRITICAL |
| `src/wallet/staking.cpp` | Wallet staking logic | CRITICAL |
| `src/wallet/staking.h` | Staking declarations | CRITICAL |
| `src/wallet/rpc/staking.cpp` | Staking RPC methods | CRITICAL |
| `src/wallet/rpc/staking.h` | Staking RPC declarations | CRITICAL |

#### Chain Parameters
| File | Purpose | Priority |
|------|---------|----------|
| `src/chainparams.cpp` | Blackcoin params, activation heights | CRITICAL |
| `src/chainparamsbase.cpp` | Base chain params | HIGH |
| `src/chainparams.h` | Chain params declarations | HIGH |
| `src/chainparamsbase.h` | Base params declarations | MEDIUM |
| `src/chainparamsseeds.h` | DNS seeds | CRITICAL |
| `src/kernel/chainparams.cpp` | DevFundAddress, checkpoints | CRITICAL |
| `src/kernel/chainparams.h` | Chain params declarations | HIGH |

#### Consensus Changes
| File | Purpose | Priority |
|------|---------|----------|
| `src/consensus/params.h` | Consensus params (nPowTargetSpacing, etc.) | CRITICAL |
| `src/consensus/validation.h` | Validation extensions | HIGH |
| `src/validation.cpp` | PoS validation logic | CRITICAL |
| `src/validation.h` | Validation declarations | HIGH |

#### Wallet Modifications
| File | Purpose | Priority |
|------|---------|----------|
| `src/wallet/wallet.cpp` | PoS transaction handling | CRITICAL |
| `src/wallet/wallet.h` | Wallet with staking | CRITICAL |
| `src/wallet/transaction.h` | Transaction with IsCoinStake | HIGH |
| `src/wallet/rpc/spend.cpp` | Spend with coinstake support | HIGH |
| `src/wallet/rpc/staking.cpp` | **Blackcoin-specific RPC calls** | CRITICAL |

#### Block/Transaction Extensions
| File | Purpose | Priority |
|------|---------|----------|
| `src/primitives/transaction.h` | IsCoinStake(), IsCoinBase() | CRITICAL |
| `src/primitives/block.h` | IsProofOfStake(), IsProofOfWork() | CRITICAL |

#### CRITICAL: Block Serialization Differences

**Blackcoin has DIFFERENT block serialization than Bitcoin:**

| Component | Bitcoin | Blackcoin | Preserved? |
|-----------|---------|-----------|------------|
| **CBlockHeader::nVersion** | 32-bit | 32-bit | YES |
| **CBlockHeader::hashPrevBlock** | uint256 | uint256 | YES |
| **CBlockHeader::hashMerkleRoot** | uint256 | uint256 | YES |
| **CBlockHeader::nTime** | uint32 | uint32 | YES |
| **CBlockHeader::nBits** | uint32 | uint32 | YES |
| **CBlockHeader::nNonce** | uint32 | uint32 | YES |
| **CBlockHeader::nFlags** | **NOT PRESENT** | **uint32 (PoS marker)** | **MUST PRESERVE** |
| **CBlock::vtx** | vector | vector | YES |
| **CBlock::vchBlockSig** | **NOT PRESENT** | **vector (PoS signature)** | **MUST PRESERVE** |

**Serialization Logic (src/primitives/block.h):**
```cpp
SERIALIZE_METHODS(CBlockHeader, obj)
{
    READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce);

    // CRITICAL: peercoin: do not serialize nFlags when computing hash
    if (!(s.GetType() & SER_GETHASH) && s.GetType() & SER_POSMARKER)
        READWRITE(obj.nFlags);
}
```

**Key Differences:**

1. **nFlags field** (uint32):
   - Only present in Blackcoin
   - Stores PoS block flags (BLOCK_STAKE_INJECTED, etc.)
   - NOT included in block hash calculation (`SER_GETHASH` excludes it)
   - Serialized with `SER_POSMARKER` for network transmission

2. **vchBlockSig** (vector<unsigned char>):
   - Only present in Blackcoin
   - Block signature for PoS blocks (proof of stake ownership)
   - Not present in Bitcoin at all

3. **GetPoWHash() method** (src/primitives/block.cpp):
   - Only in Blackcoin for legacy block compatibility
   - **Scrypt** - Used ONLY for syncing **old historical PoW blocks** (nVersion <= 6)
   - **SHA256** - Used for all **new blocks** (nVersion > 6) - same as Bitcoin
   - **NOT used on mainnet** - Mainnet is PoS-only!
   - **PoW reward on testnet: 10000 BLK**

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

**Important: Scrypt is ONLY for syncing old historical blocks. New blocks use SHA256 (same as Bitcoin).**

4. **Serialization Flags** (src/serialize.h):
   - `SER_GETHASH` (1 << 2) - For computing hash (excludes nFlags)
   - `SER_POSMARKER` (1 << 18) - For sending block headers with PoS marker

**DO NOT PORT from Bitcoin 30.x:**
- Any changes to CBlockHeader structure
- Bitcoin's block serialization without nFlags
- Any removal of vchBlockSig

**Files to restore:**
- `src/primitives/block.h` - Complete with nFlags and vchBlockSig
- `src/primitives/block.cpp` - GetHash() and GetPoWHash() with Scrypt
- `src/serialize.h` - SER_POSMARKER flag definition
- `src/validation.cpp` - CheckBlockSignature() using vchBlockSig
- `src/crypto/scrypt.cpp` - Scrypt implementation for GetPoWHash()
- `src/crypto/scrypt-sse2.cpp` - SSE2-optimized Scrypt

#### RPC Extensions
| File | Purpose | Priority |
|------|---------|----------|
| `src/rpc/blockchain.cpp` | getblock with PoS info | HIGH |
| `src/rpc/mining.cpp` | generate, getblocktemplate with PoS | HIGH |
| `src/wallet/rpc/staking.cpp` | Blackcoin staking RPC methods | CRITICAL |

### 1.2 Blackcoin-Specific RPC Calls (MUST PRESERVE)

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

**Note**: Staking RPC calls are in `src/wallet/rpc/staking.cpp`. Burn and optimize UTXO calls are in `src/wallet/rpc/spend.cpp`. Both files must be preserved exactly.

### 1.2 Bitcoin Files with Blackcoin Modifications

| File | Change Type | Preserve? |
|------|-------------|-----------|
| `src/chain.h` | PoS block flags | YES |
| `src/coins.cpp` | IsCoinStake checks | YES |
| `src/coins.h` | Coinstake UTXO handling | YES |
| `src/addresstype.cpp` | Blackcoin address types | PARTIAL |
| `src/txmempool.cpp` | Coinstake handling | PARTIAL |
| `src/txmempool.h` | Coinstake tracking | PARTIAL |
| `src/init.cpp` | Staking startup | YES |
| `src/net.cpp` | Seeders, ports | YES |
| `src/net_processing.cpp` | PoS block handling | PARTIAL |

### 1.3 Files to REPLACE from Bitcoin 30.2.0

| Category | Files |
|----------|-------|
| Networking | `src/net.cpp`, `src/net.h`, `src/net_processing.cpp` (most) |
| Mempool | `src/txmempool.cpp`, `src/txmempool.h` (core logic) |
| Validation | `src/validation.cpp`, `src/validation.h` (core logic) |
| Consensus | `src/consensus/*` (adapt params) |
| RPC | Most of `src/rpc/*` |
| Wallet | `src/wallet/*` (adapt, not replace) |
| GUI | `src/qt/*` |
| **Scrypt/SSE2** | `src/crypto/scrypt-sse2.cpp`, configure.ac SSE2 flag | **PRESERVE** |

### 1.4 External Subtrees (UPDATE NORMALLY)

| Directory | Action |
|-----------|--------|
| `src/secp256k1/` | Pull latest from Bitcoin 30.x |
| `src/leveldb/` | Pull latest from Bitcoin 30.x |
| `src/minisketch/` | Pull latest from Bitcoin 30.x |
| `src/crc32c/` | Pull latest from Bitcoin 30.x |

### 1.5 Files NOT to Change

These files are Blackcoin-specific and should be preserved exactly as-is:

| File/Folder | Reason |
|-------------|--------|
| `.github/workflows/build.yml` | Blackcoin CI build configuration |
| `.github/workflows/docker_build_push_26.yml` | Blackcoin Docker build |
| `.gitignore` | Blackcoin-specific ignore rules section |
| `configure.ac` | SSE2 flag for scrypt (speeds up syncing older blocks) |
| `src/crypto/scrypt-sse2.cpp` | SSE2 optimization for Scrypt |
| `src/crypto/scrypt.cpp` | Scrypt implementation |
| `src/wallet/init.cpp` | Blackcoin-specific args: -staketimio, -stakecache, -donatetodevfund |
| `src/policy/policy.h` | Static fee constants (RBF DISABLED) |
| `src/kernel/mempool_options.h` | Min relay fee configuration |
| `src/kernel/chainparams.cpp` | Taproot NEVER_ACTIVE, DevFundAddress |
| `src/deploymentinfo.cpp` | Taproot deployment info |
| `test/functional/` | Tests never adapted for Blackcoin - may not work |
| `contrib/guix/` | GUIX never tested or adapted |
| `contrib/init/` | Blackcoin-specific init scripts (blackmored, etc.) |
| `doc/man/*.1` | Blackcoin man pages |

### 1.6 Items NOT to Port from Bitcoin 30.x

These Bitcoin 30.x features must NOT be ported:

| Feature | Reason |
|---------|--------|
| RBF (`-walletrbf`, `-enable-rbf`) | RBF is DISABLED in Blackcoin |
| Fee estimation (`src/kernel/feerate*.cpp`) | Blackcoin uses STATIC fees |
| Dynamic fee calculation | Blackcoin has hardcoded fee constants |
| Taproot activation | Taproot is coded but NEVER_ACTIVE |

### 1.7 Blackcoin-Specific GUI Quirks

The following GUI files contain Blackcoin-specific modifications that MUST be preserved:

| File | Purpose | Modification |
|------|---------|--------------|
| `src/qt/bitcoingui.cpp` | Main GUI window | Staking status, Blackcoin terminology, wallet actions |
| `src/qt/overviewpage.cpp` | Overview page | Staking weight display, CanStake() integration |
| `src/qt/walletmodel.cpp` | Wallet model | Stake weight, coinstake tracking |
| `src/qt/walletmodel.h` | Wallet model header | getStakeWeight(), CanStake() |
| `src/qt/transactionrecord.cpp` | Transaction records | CoinStake transaction display |
| `src/qt/transactionrecord.h` | Transaction record header | IsCoinStake flag |
| `src/qt/transactiontablemodel.cpp` | Transaction table | Coinstake filtering/sorting |
| `src/qt/transactiondesc.cpp` | Transaction description | Coinstake description |
| `src/qt/askpassphrasedialog.cpp` | Passphrase dialog | Blackcoin-specific warnings |
| `src/qt/res/icons/tx_staked.png` | Staked transaction icon | Custom icon for coinstake txs |
| `src/qt/locale/*.ts` | Translations | Blackcoin terminology (BLK, staking) |
| `src/qt/forms/ui_*.h` | UI forms | Blackcoin-specific labels |

#### GUI Labels and Terminology
- "BLK" unit instead of "BTC"
- "Blackcoins" instead of "bitcoins"
- "Stake" instead of "Mint" (or as applicable)
- Blackcoin-specific warnings in passphrase dialog
- Staking weight and status in overview page
- CoinStake transaction icons

**Note**: The GUI contains extensive Blackcoin-specific terminology and staking UI elements. These must be carefully preserved during the upgrade.

**Note**: Functional tests (`test/functional/*`) were never adapted for Blackcoin's PoS implementation. They may fail or be incompatible. Do not attempt to fix them as part of this upgrade - focus on core functionality first.

---

## Phase 2: Preparation

### 2.1 Create Upgrade Branches

```bash
# Main upgrade branch
git checkout -b upgrade/30.2.0

# Temporary branches
git checkout -b backup/v26.2.0          # Backup original state
git checkout -b analysis/bitcoin-30.2.0 # Fresh Bitcoin 30.2.0
```

### 2.2 Fetch Bitcoin 30.2.0

```bash
# Add Bitcoin remote if not present
git remote add Bitcoin https://github.com/bitcoin/bitcoin.git
git fetch Bitcoin

# Track Bitcoin 30.2.0
git checkout -b bitcoin-30.2.0 Bitcoin/30.2.0
```

### 2.3 Create File Inventory

```bash
# Generate list of Blackcoin-specific files
cat > scripts/file_inventory.py << 'EOF'
#!/usr/bin/env python3
import subprocess
import os

# Get all modified files from Bitcoin 26.x to current
result = subprocess.run(
    ['git', 'diff', '--name-only', 'Bitcoin/26.x..HEAD'],
    capture_output=True, text=True, cwd='/path/to/repo'
)
files = result.stdout.strip().split('\n')

# Categorize files
critical = []   # PoS, staking, chainparams
high = []       # Wallet modifications
medium = []     # RPC modifications
low = []        # Build/config files

for f in files:
    if any(x in f for x in ['pos.cpp', 'pos.h', 'staking', 'chainparams', 
                             'consensus/params', 'validation.cpp']):
        critical.append(f)
    elif 'wallet' in f:
        high.append(f)
    elif 'rpc' in f:
        medium.append(f)
    elif any(x in f for x in ['Makefile', 'configure', '.github', 'contrib']):
        low.append(f)

print("=== CRITICAL (PoS/Staking/Chainparams) ===")
for f in critical: print(f)
print("\n=== HIGH (Wallet) ===")
for f in high: print(f)
print("\n=== MEDIUM (RPC) ===")
for f in medium: print(f)
print("\n=== LOW (Build/Config) ===")
for f in low: print(f)
EOF
```

---

## Phase 3: Incremental Upgrade Strategy

### 3.1 Recommended: Incremental Approach

Rather than merging 26.x → 30.x directly, consider:

```
26.2.0 → 27.x → 28.x → 29.x → 30.2.0
```

Each step is smaller and more manageable.

### 3.2 Direct Merge Approach (Alternative)

If doing direct merge:

```bash
# Reset to Bitcoin 30.2.0
git checkout -b merge-30.2.0 bitcoin-30.2.0
git reset --hard bitcoin-30.2.0

# Merge Blackcoin with strategy ours
git merge --strategy=ours --no-commit backup/v26.2.0

# Restore Blackcoin-specific files (in order):
```

### 3.3 File Restoration Order

#### Step 1: Core PoS Files
```bash
git checkout backup/v26.2.0 -- \
  src/pos.cpp src/pos.h \
  src/primitives/transaction.h src/primitives/block.h
```

#### Step 2: Consensus Parameters
```bash
git checkout backup/v26.2.0 -- \
  src/consensus/params.h \
  src/kernel/chainparams.cpp src/kernel/chainparams.h \
  src/chainparams.cpp src/chainparams.h \
  src/chainparamsbase.cpp src/chainparamsbase.h \
  src/chainparamsseeds.h
```

#### Step 3: Validation Logic
```bash
git checkout backup/v26.2.0 -- \
  src/validation.cpp src/validation.h \
  src/consensus/validation.h
```

#### Step 4: Wallet with Staking
```bash
git checkout backup/v26.2.0 -- \
  src/wallet/wallet.cpp src/wallet/wallet.h \
  src/wallet/transaction.h \
  src/wallet/staking.cpp src/wallet/staking.h \
  src/wallet/rpc/staking.cpp src/wallet/rpc/staking.h \
  src/wallet/init.cpp  # Blackcoin-specific args: -staketimio, -stakecache, -donatetodevfund
```

#### Step 5: CI/CD (PRESERVE EXACTLY)
```bash
git checkout backup/v26.2.0 -- \
  .github/workflows/build.yml \
  .github/workflows/docker_build_push_26.yml \
  .gitignore
```

#### Step 6: Scrypt/SSE2 (PRESERVE EXACTLY)
```bash
git checkout backup/v26.2.0 -- \
  configure.ac \
  src/crypto/scrypt-sse2.cpp \
  src/crypto/scrypt.cpp \
  src/crypto/scrypt.h \
  src/Makefile.am
```

**IMPORTANT**: The SSE2 flag in `configure.ac` (`--enable-sse2`) is critical for speeding up syncing of older blocks that use Scrypt hashing.

#### Step 7: BerkeleyDB 6.2 Support (PRESERVE EXACTLY)
```bash
git checkout backup/v26.2.0 -- \
  configure.ac \
  src/wallet/db.cpp src/wallet/db.h \
  src/wallet/bdb.cpp \
  depends/packages/bdb.mk
```

**IMPORTANT**: BDB 6.2 is required for existing Blackcoin wallets to open.

#### Step 8: Additional Modifications
```bash
git checkout backup/v26.2.0 -- \
  src/chain.cpp src/chain.h \
  src/coins.cpp src/coins.h \
  src/addresstype.cpp \
  src/init.cpp
```

#### Step 8: GUI (PRESERVE EXACTLY)
```bash
git checkout backup/v26.2.0 -- \
  src/qt/bitcoingui.cpp \
  src/qt/overviewpage.cpp \
  src/qt/walletmodel.cpp src/qt/walletmodel.h \
  src/qt/transactionrecord.cpp src/qt/transactionrecord.h \
  src/qt/transactiontablemodel.cpp \
  src/qt/transactiondesc.cpp \
  src/qt/askpassphrasedialog.cpp \
  src/qt/res/icons/tx_staked.png
```

**IMPORTANT**: GUI contains extensive Blackcoin-specific terminology (BLK units, staking UI, warnings).

#### Step 9: Final Configuration Files
```bash
git checkout backup/v26.2.0 -- \
  src/Makefile.am \
  src/Makefile.test.include \
  src/Makefile.qt.include \
  src/Makefile.qttest.include \
  src/Makefile.bench.include
```

---

## Phase 4: API Compatibility

### 4.1 Expected Bitcoin 30.x API Changes

#### Type Changes
| Bitcoin 26.x | Bitcoin 30.x |
|--------------|--------------|
| `uint256` | `Txid` / `Wtxid` (typed) |
| `COutPoint::hash` (uint256) | `COutPoint::hash` (Txid) |
| `vTxHashes` | `txns_randomized` |
| `queryHashes()` | `entryAll()` iteration |
| `CDataStream` | `DataStream` |

#### Function Changes
| Bitcoin 26.x | Bitcoin 30.x |
|--------------|--------------|
| `IsCoinBase()` | (unchanged) |
| `IsCoinStake()` | ADD THIS |
| `IsProofOfStake()` | ADD THIS |
| `IsProofOfWork()` | ADD THIS |
| `nTargetSpacing` | `nPowTargetSpacing` |
| `GetAdjustedTime()` | REMOVED |
| `ShutdownRequested()` | `ShutdownRequested(node)` |
| `CreateNewBlock()` | Signature changed |
| `CheckBlock()` | Signature changed |

#### New Requirements
- **C++20** required (no C++17)
- **Python 3.9+** for tests
- **Boost 1.77.0+**

#### CRITICAL: BerkeleyDB 6.2 Support Required

**Blackcoin More uses BDB 6.2** for wallet storage. This MUST be preserved.

| Bitcoin Version | Wallet Database |
|-----------------|-----------------|
| 26.x | BDB 4.8 (deprecated) or SQLite |
| 30.x | Bitcoin removed BDB, Blackcoin MUST keep BDB 6.2 |

**Blackcoin More v30.2.0 Requirements:**
1. Keep BDB 6.2 wallet support (existing wallets must open)
2. Keep SQLite wallet support (new wallets can use SQLite)
3. Add `deprecatedrpc=create_bdb` setting to allow creating new BDB wallets

**Port BDB 6.2 from Blackcoin to Bitcoin 30.x:**
- Restore `src/wallet/db.h`, `src/wallet/db.cpp` with BDB 6.2 support
- Restore BDB 6.2 configure options in `configure.ac`
- Restore `src/wallet/bdb.cpp` with BDB 6.2
- Add `deprecatedrpc=create_bdb` to allow BDB wallet creation

**Configuration for BDB wallets:**
```bash
# In blackmore.conf to create new BDB wallets:
deprecatedrpc=create_bdb
```
- **Clang 15+** or **GCC 12+**

### 4.2 Compatibility Fixes Needed

#### Add to CTransaction
```cpp
// In src/primitives/transaction.h
bool IsCoinStake() const {
    return (vin.size() > 0 && (!vin[0].prevout.IsNull()) 
            && vout.size() >= 2 && vout[0].IsEmpty());
}
```

#### Add to CBlock
```cpp
// In src/primitives/block.h
bool IsProofOfStake() const {
    return (vtx.size() > 1 && vtx[1]->IsCoinStake());
}
bool IsProofOfWork() const {
    return !IsProofOfStake();
}
```

#### Add to CTxOut
```cpp
// In src/primitives/transaction.h
bool IsEmpty() const {
    return (nValue == 0 && scriptPubKey.empty());
}
```

#### Fix Consensus Params
```cpp
// In src/consensus/params.h
int64_t nPowTargetSpacing;  // Changed from nTargetSpacing
```

#### Fix Mempool RPC
```cpp
// In src/rpc/mempool.cpp
// Before:
info.pushKV("wtxid", pool.vTxHashes[e.vTxHashesIdx].first.ToString());

// After:
info.pushKV("wtxid", e.GetTx().GetWitnessHash().ToString());

// Before:
pool.queryHashes(vtxid);

// After:
for (const CTxMemPoolEntry& e : pool.entryAll()) {
    vtxid.push_back(e.GetTx().GetHash());
}
```

#### Fix String Conversions
```cpp
// Before:
path.u8string()

// After:
path.utf8string()
```

---

## Phase 5: Testing Strategy

### 5.1 Build Testing

#### Linux/Standard Build
```bash
# Configure with C++20 (DO NOT forget SSE2 for Scrypt!)
./autogen.sh
./configure CXXFLAGS="-std=c++20" --enable-tests --enable-sse2

# Build
make -j$(nproc)

# Run unit tests (if they pass)
src/test/test_blackmore

# NOTE: Functional tests were never adapted for Blackcoin
# They may fail or be incompatible with PoS functionality
# test/functional/test_runner.py  # May not work
```

#### macOS Build (Using depends/)
```bash
# Step 1: Build dependencies in depends folder
cd depends
make HOST=x86_64-apple-darwin18 -j$(nproc)
cd ..

# Step 2: Configure using depends (VERSION MUST MATCH the HOST above!)
CONFIG_SITE=$PWD/depends/x86_64-apple-darwin18/share/config.site \
  ./configure \
  --enable-reduce-exports \
  --disable-bench \
  --disable-tests \
  --disable-gui-tests \
  --disable-man \
  --enable-upnp-default \
  --prefix=/

# Step 3: Build
make -j$(nproc)
```

**IMPORTANT**: The `HOST=` version in step 1 and the `depends/` directory name in step 2 MUST match exactly.

#### Wallet Configuration for BDB Support
```bash
# In blackmore.conf to create new BDB wallets:
deprecatedrpc=create_bdb
```

This allows creating new BDB 6.2 wallets (useful for recovery or legacy use).

#### Blackcoin-Specific Configuration Settings

These settings MUST be preserved in the upgrade:

| Setting | Purpose | Default | Range | File |
|---------|---------|---------|-------|------|
| `-staketimio=<n>` | Proof of stake timeout (ms) | 500 | - | `src/wallet/init.cpp` |
| `-stakecache=<true/false>` | Enable staking cache | false | - | `src/wallet/init.cpp` |
| `-donatetodevfund=<n>` | Donate % of staking rewards | 20 | 0-95 | `src/wallet/init.cpp` |
| `-txversion=<n>` | Transaction version | 2 | - | `src/wallet/init.cpp` |
| `-staking=<true/false>` | Enable/disable staking | true | - | `src/wallet/init.cpp` |
| `-checkkernel=<true/false>` | Check if kernel can stake | - | - | `src/wallet/init.cpp` |
| `-reservebalance=<n>` | Reserve balance from staking | 0 | - | `src/wallet/init.cpp` |

**Dev Fund Configuration:**
- Dev Fund address is hardcoded in `src/kernel/chainparams.cpp`
- Mainnet: `BKDvboD1CzZ5KycP1FRSXRoi7XXhHoQhS1`
- Testnet: `n14L5xqAs7QRzNiTLPNaPeqaF9CRoxzVnU`
- Regtest: empty (no dev fund)

#### Blackcoin Network Configuration

**Network Ports:**
| Network | P2P Port | RPC Port |
|---------|----------|----------|
| Mainnet | 15714 | 15715 |
| Testnet | 25714 | 25715 |
| Regtest | 25714 | 25715 |

**Files to restore:**
- `src/kernel/chainparams.cpp` - Contains port configuration for each network

#### Blackcoin Stake Parameters (Consensus)

These parameters are defined in `src/consensus/params.h` and `src/kernel/chainparams.cpp`:

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `nCoinbaseMaturity` | 500 | UTXO age required before it can stake |
| `nMaxReorganizationDepth` | 500 | Maximum reorg depth |
| `nStakeTimestampMask` | 0xf (15) | Stake timestamp granularity (must be divisible by 16 seconds) |
| `nTargetSpacing` | 64 seconds | Block spacing |
| `nTargetTimespan` | 24 hours | Difficulty adjustment period |
| `nLastPOWBlock` | Various | Last block where PoW is allowed (testnet only) |

**Files to restore:**
- `src/consensus/params.h` - Stake parameter declarations
- `src/kernel/chainparams.cpp` - Stake parameter values for each network

#### Blackcoin-Specific Fee and Policy Settings

**RBF (Replace-by-Fee):**
- **RBF is DISABLED in Blackcoin** - Must stay disabled
- No `-walletrbf` or `-enable-rbf` option exists
- All RBF-related code removed from wallet initialization

**Fee Structure:**

| Component | Blackcoin | Bitcoin |
|-----------|-----------|---------|
| **PoS Reward** | Fixed 1.5 BLK subsidy + tx fees | N/A (PoS-only) |
| **PoW Block** | GetBlockSubsidy() + nFees | GetBlockSubsidy() + nFees |
| **Relay Fee** | Static 100000 satoshis/kvB | Dynamic estimation |
| **User Fee Setting** | `-paytxfee` (manual) | `-paytxfee` or dynamic |
| **Fee Estimation** | NONE | Sophisticated algorithm |

**Blackcoin Fee Constants:**
| Constant | Value | Purpose |
|----------|-------|---------|
| `DEFAULT_MIN_RELAY_TX_FEE` | 100000 satoshis/kvB | Minimum relay threshold |
| `DUST_RELAY_TX_FEE` | 100000 satoshis/kvB | Dust limit threshold |
| `GetProofOfStakeSubsidy()` | COIN * 3 / 2 | Fixed PoS reward (1.5 BLK) |

**Key Differences from Bitcoin:**
1. **No dynamic fee estimation** - Blackcoin has NO fee estimation code
2. **Fixed PoS subsidy** - `GetProofOfStakeSubsidy()` returns exactly 1.5 BLK
3. **Coinstake tx fees** - The reward is `outputs - inputs` in coinstake transaction
4. **Static relay fees** - 100000 satoshis/kvB minimum for all transactions

**Files to restore:**
- `src/policy/policy.h` - Contains fee constants (100000 satoshis/kvB)
- `src/wallet/init.cpp` - Contains `-paytxfee` argument
- `src/validation.cpp` - Contains `GetProofOfStakeSubsidy()` (1.5 BLK fixed)

**DO NOT port from Bitcoin 30.x:**
- Fee estimation code (`src/kernel/feerate*.cpp`, `src/kernel/fee*.cpp`)
- RBF wallet options (`-walletrbf`, `-enable-rbf`)
- Dynamic fee estimation logic
- Any mempool fee priority logic

#### Taproot Configuration

**IMPORTANT:** Taproot code is in place but DISABLED (NEVER_ACTIVE)

| Network | Taproot Status | Activation |
|---------|----------------|------------|
| Mainnet | Code present, never activates | `nStartTime = NEVER_ACTIVE` |
| Testnet | Code present, never activates | `nStartTime = NEVER_ACTIVE` |
| Signet | Code present, never activates | `nStartTime = NEVER_ACTIVE` |
| Regtest | Code present, never activates | `nStartTime = NEVER_ACTIVE` |

**Files to preserve:**
- `src/deploymentinfo.cpp` - Taproot deployment info (exists)
- `src/kernel/chainparams.cpp` - Taproot config with NEVER_ACTIVE

**Files NOT to change:**
- `src/consensus/params.h` - DEPLOYMENT_TAPROOT enum (preserve)

**Note:** When SegWit activates on mainnet (June 20, 2025), Taproot can be enabled by changing `nStartTime` from `NEVER_ACTIVE` to a future date.

**Example blackmore.conf:**
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

### 5.2 Critical Tests

| Test | Purpose | Status |
|------|---------|--------|
| `getstakinginfo` RPC | Returns staking stats (enabled, staking, pool size, etc.) | REQUIRED |
| `staking` RPC | Enable/disable wallet staking | REQUIRED |
| `reservebalance` RPC | Reserve balance from staking | REQUIRED |
| `checkkernel` RPC | Check if kernel can stake | REQUIRED |
| `burn` RPC | Burn coins to unspendable address | REQUIRED |
| `burnwallet` RPC | Burn all wallet UTXOs | REQUIRED |
| `optimizeutxoset` RPC | Optimize UTXO set consolidation | REQUIRED |
| Manual staking test | Stake on regtest | REQUIRED |
| Manual sync test | Sync from scratch | REQUIRED |
| Manual tx test | Send transactions | REQUIRED |
| `wallet_staking.py` | Functional test | NOT ADAPTED |
| `wallet_coinstake.py` | Functional test | NOT ADAPTED |
| `rpc_getblock.py` | Functional test | NOT ADAPTED |

**Note**: Functional tests were never adapted for Blackcoin. Do not expect them to pass. Focus on manual testing of core staking and transaction functionality.

### 5.3 Network Testing Strategy

#### Testnet Testing (FIRST - PRIORITY)
Testnet supports **both PoW mining AND PoS staking** (unlike mainnet which is PoS-only).

| Phase | Test | Description |
|-------|------|-------------|
| 1 | Sync from scratch | Download and validate entire testnet blockchain |
| 2 | Mining (PoW) | Test PoW block production (reward: 10000 BLK, uses SHA256) |
| 3 | Staking (PoS) | Test stake generation and validation |
| 4 | Transactions | Send regular transactions and coinstakes |

**Note**: Scrypt is ONLY for syncing **old historical testnet blocks**. New testnet blocks use **SHA256** (same as Bitcoin). SSE2 accelerates sync of old blocks.

#### Mainnet Testing (AFTER TESTNET)
**Mainnet is PoS-only - NO mining allowed!** SegWit BIP-9 softfork is still running (activation: June 20, 2025).

| Phase | Test | Description |
|-------|------|-------------|
| 1 | Sync from scratch | Download and validate mainnet blockchain |
| 2 | Staking (PoS) | Test stake generation (CRITICAL - mainnet is PoS-only) |
| 3 | Transactions | Send transactions |
| 4 | SegWit rules | Verify SegWit enforcement (when active) |

**IMPORTANT**: SSE2 flag (`--enable-sse2`) accelerates syncing **old historical blocks** that use Scrypt. **NO PoW on mainnet!**

---

## Phase 6: Version Bump

### 6.1 Update Version Numbers

File: `configure.ac`
```diff
define(_CLIENT_VERSION_MAJOR, 26)
define(_CLIENT_VERSION_MINOR, 2)
+define(_CLIENT_VERSION_MAJOR, 27)  # or 30
+define(_CLIENT_VERSION_MINOR, 2)
```

### 6.2 Update Branding

| File | Update |
|------|--------|
| `src/clientversion.h` | Version strings |
| `src/qt/res/icons/*.png` | Icons |
| `doc/man/*.1` | Man pages |
| `share/examples/*.conf` | Config files |

---

## Phase 7: Checklists

### 7.1 Pre-Merge Checklist
- [ ] Backup current v26.2.0 branch
- [ ] Fetch Bitcoin 30.2.0
- [ ] Create file inventory of Blackcoin-specific code
- [ ] Document all API changes between 26.x and 30.x
- [ ] **Port BDB 6.2 support** from Blackcoin to Bitcoin 30.x (Bitcoin removed BDB!)
- [ ] Add `deprecatedrpc=create_bdb` to allow BDB wallet creation
- [ ] **DO NOT** modify CI/CD workflows (build.yml, docker_build_push_26.yml)
- [ ] **DO NOT** modify GUIX (never adapted for Blackcoin)
- [ ] Create test environment

### 7.2 Merge Checklist
- [ ] Reset to Bitcoin 30.2.0
- [ ] Merge Blackcoin with strategy=ours
- [ ] Restore PoS files (pos.cpp, pos.h)
- [ ] Restore chain parameters
- [ ] Restore wallet/staking files
- [ ] **Restore BDB 6.2 support** (db.cpp, db.h, bdb.cpp, configure.ac BDB options)
- [ ] **Preserve** CI/CD workflows (build.yml, docker_build_push_26.yml)
- [ ] **Preserve** contrib/init/ (blackmored scripts)
- [ ] Fix API incompatibilities
- [ ] Update version to 30.2.0
- [ ] Build successfully
- [ ] Pass unit tests (if any pass)
- [ ] **Manual testing only** for functional tests (never adapted)
- [ ] Update AGENTS.md

### 7.3 Post-Merge Checklist
- [ ] Update AGENTS.md with new build/test commands
- [ ] **DO NOT update CI/CD** (keep as-is)
- [ ] Create release notes
- [ ] Update documentation
- [ ] Test upgrade from v26.2.0 (manual)
- [ ] Test wallet upgrade path (manual)

#### Testnet Testing (FIRST)
- [ ] Testnet: Sync from scratch - REQUIRED
- [ ] Testnet: Mining (PoW) - REQUIRED
- [ ] Testnet: Staking (PoS) - REQUIRED
- [ ] Testnet: Send transactions - REQUIRED

#### Mainnet Testing (AFTER TESTNET)
- [ ] Mainnet: Sync from scratch - REQUIRED
- [ ] Mainnet: Staking only (PoS-only, NO mining) - CRITICAL
- [ ] Mainnet: Send transactions - REQUIRED
- [ ] Mainnet: SegWit enforcement (when activated June 20, 2025)

---

## Estimated Effort

| Phase | Effort | Notes |
|-------|--------|-------|
| Analysis & Inventory | 2-4 hours | |
| Preparation | 1-2 hours | |
| Core Merge | 4-8 hours | |
| API Fixes | 4-8 hours | |
| Testing | 8-16 hours | Manual testing only (tests never adapted) |
| Documentation | 2-4 hours | |
| **Total** | **21-42 hours** | Mostly manual testing |

**Note**: Testing effort is higher because functional tests were never adapted for Blackcoin. Expect to spend significant time on manual validation of staking, transactions, and sync.

---

## Rollback Plan

If issues arise:

```bash
# Restore from backup
git checkout backup/v26.2.0
git branch -D upgrade/30.2.0

# Or revert merge commits
git revert <merge-commit-1>
git revert <merge-commit-2>
```

---

## References

- Bitcoin Core Release Notes: https://github.com/bitcoin/bitcoin/blob/master/doc/release-notes.md
- Bitcoin Core Git History: https://github.com/bitcoin/bitcoin/commits/master
- Blackcoin More Repository: https://github.com/CoinBlack/blackcoin-more

---

*Last Updated: January 19, 2026*
*Version: 1.0*
