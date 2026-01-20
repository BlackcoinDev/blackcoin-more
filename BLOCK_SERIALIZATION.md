# Blackcoin More Block Serialization

**Critical PoS-Specific Block Structure**

---

## Overview

Blackcoin More uses a **Proof-of-Stake (PoS)** block structure that extends Bitcoin Core's block format with PoS-specific fields. These fields are **NOT present in Bitcoin Core** and must be preserved during the Bitcoin 26.x → 30.x upgrade.

---

## Block Structure Comparison

### Bitcoin Core vs Blackcoin More

| Component | Bitcoin Core | Blackcoin More | Preserved |
|-----------|--------------|----------------|-----------|
| `CBlockHeader::nFlags` | ❌ Not present | ✅ `uint32_t` | **CRITICAL** |
| `CBlock::vchBlockSig` | ❌ Not present | ✅ `vector<unsigned char>` | **CRITICAL** |
| `CBlockIndex::nStakeModifier` | ❌ Not present | ✅ `uint256` | **CRITICAL** |
| `SERIALIZE_METHODS` | Standard | Uses `SER_POSMARKER` | **CRITICAL** |

---

## CBlockHeader Structure

```cpp
// src/primitives/block.h

class CBlockHeader
{
public:
    // Standard Bitcoin fields (80 bytes)
    int32_t nVersion;           // Block version
    uint256 hashPrevBlock;      // Previous block hash
    uint256 hashMerkleRoot;     // Merkle root of transactions
    uint32_t nTime;             // Block timestamp (Unix time)
    uint32_t nBits;             // Difficulty target (compact)
    uint32_t nNonce;            // PoW nonce

    // ⚠️ CRITICAL: Blackcoin/Peercoin extension - NOT in Bitcoin Core
    uint32_t nFlags;            // PoS block marker (BLOCK_PROOF_OF_STAKE)

    // Serialization includes nFlags only with SER_POSMARKER
    SERIALIZE_METHODS(CBlockHeader, obj)
    {
        READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, 
                  obj.nTime, obj.nBits, obj.nNonce);

        // peercoin: do not serialize nFlags when computing hash
        // nFlags is only included when:
        // 1. NOT computing hash (SER_GETHASH not set)
        // 2. SER_POSMARKER flag is set (for network transmission)
        if (!(s.GetType() & SER_GETHASH) && (s.GetType() & SER_POSMARKER))
            READWRITE(obj.nFlags);
    }
};
```

### Header Size

| Field | Bitcoin Core | Blackcoin More (network) | Blackcoin More (hash) |
|-------|--------------|--------------------------|----------------------|
| Base header | 80 bytes | 80 bytes | 80 bytes |
| nFlags | ❌ | +4 bytes (conditional) | ❌ |
| **Total** | **80 bytes** | **84 bytes** | **80 bytes** |

---

## nFlags Field Details

### Definition (src/chain.h)

```cpp
// Block index flags - stored in CBlockIndex::nFlags
enum
{
    BLOCK_PROOF_OF_STAKE = (1 << 0),   // Block is Proof-of-Stake
    BLOCK_STAKE_ENTROPY  = (1 << 1),   // Stake entropy bit
    BLOCK_STAKE_MODIFIER = (1 << 2),   // Stake modifier flag
};
```

### Purpose

| Flag | Value | Purpose |
|------|-------|---------|
| `BLOCK_PROOF_OF_STAKE` | `0x01` | Marks block as PoS (vs PoW) |
| `BLOCK_STAKE_ENTROPY` | `0x02` | Used for stake modifier calculation |
| `BLOCK_STAKE_MODIFIER` | `0x04` | Indicates valid stake modifier |

### Usage in Validation

```cpp
// src/chain.h

bool IsProofOfStake() const
{
    return (nFlags & BLOCK_PROOF_OF_STAKE);
}

void SetProofOfStake()
{
    nFlags |= BLOCK_PROOF_OF_STAKE;
}
```

### Serialization Flow

```
Network Transmission:
  CBlockHeader → SER_NETWORK | SER_POSMARKER → includes nFlags
                ↓
  Deserialization checks SER_POSMARKER flag
                ↓
  nFlags extracted and stored in CBlockIndex

Block Hash Calculation:
  CBlockHeader → SER_GETHASH → nFlags EXCLUDED
                ↓
  Hash computed from standard 80-byte header
                ↓
  nFlags not part of hash (prevents hash malleability attacks)
```

---

## CBlock Structure

```cpp
// src/primitives/block.h

class CBlock : public CBlockHeader
{
public:
    std::vector<CTransactionRef> vtx;           // Transactions

    // ⚠️ CRITICAL: Block signature - NOT in Bitcoin Core
    // Contains ECDSA signature proving stake ownership
    // Created by signing block hash with coinstake input key
    std::vector<unsigned char> vchBlockSig;     // Max 65 bytes (compact signature)

    SERIALIZE_METHODS(CBlock, obj)
    {
        READWRITE(AsBase<CBlockHeader>(obj), obj.vtx, obj.vchBlockSig);
    }

    // Determine if block is PoS or PoW
    bool IsProofOfStake() const
    {
        return (vtx.size() > 1 && vtx[1]->IsCoinStake());
    }

    bool IsProofOfWork() const
    {
        return !IsProofOfStake();
    }
};
```

### Coinstake Transaction Pattern

```cpp
// PoS block has 2+ transactions:
// vtx[0] = coinbase (empty for PoS blocks)
// vtx[1] = coinstake (first output is empty, marks stake ownership)
// vtx[2..n] = regular transactions
```

---

## vchBlockSig (Block Signature)

### Creation (src/node/miner.cpp)

```cpp
// PoS block signing in CreateNewBlock()
return key.Sign(block.GetHash(), block.vchBlockSig, 0);

// Or via keystore:
SigningResult res = keystore.SignBlockHash(
    block.GetHash(), *pkhash, block.vchBlockSig);
```

### Verification (src/validation.cpp:3549)

```cpp
static bool CheckBlockSignature(const CBlock& block)
{
    // PoW blocks have no signature
    if (block.IsProofOfWork())
        return block.vchBlockSig.empty();

    // PoS blocks MUST have signature
    if (block.vchBlockSig.empty())
        return false;

    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.vtx[1]->vout[1];  // Second output of coinstake

    TxoutType whichType = Solver(txout.scriptPubKey, vSolutions);

    if (whichType == TxoutType::PUBKEY) {
        // Direct public key in output
        std::vector<unsigned char>& vchPubKey = vSolutions[0];
        return CPubKey(vchPubKey).Verify(block.GetHash(), block.vchBlockSig);
    }
    else {
        // Public key in OP_RETURN (nonspendable output)
        // Allows multisig staking without polluting UTXO set
        const CScript& script = txout.scriptPubKey;
        CScript::const_iterator pc = script.begin();
        opcodetype opcode;
        std::vector<unsigned char> vchPushValue;

        uint256 hash = block.GetHash();

        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (opcode != OP_RETURN)
            return false;
        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (!IsCompressedOrUncompressedPubKey(vchPushValue))
            return false;
        return CPubKey(vchPushValue).Verify(hash, block.vchBlockSig);
    }
}
```

### Signature Format

| Field | Size | Description |
|-------|------|-------------|
| `vchBlockSig[0]` | 1 byte | Signature type (usually `0x00` for low-S) |
| `vchBlockSig[1-33]` | 33 bytes | R component (X coordinate of ECDSA signature) |
| `vchBlockSig[34-65]` | 32 bytes | S component (Y coordinate of ECDSA signature) |
| **Total** | **Up to 65 bytes** | Standard ECDSA compact signature |

---

## Block Hash Calculation

### Hash Algorithm (src/primitives/block.cpp:14)

```cpp
uint256 CBlockHeader::GetHash() const
{
    if (nVersion > 6)
        return (CHashWriter{} << *this).GetHash();
    return GetPoWHash();
}

// For nVersion <= 6, uses scrypt (legacy PoW)
uint256 CBlockHeader::GetPoWHash() const
{
    uint256 thash;
    scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
    return thash;
}
```

### Important: nFlags is EXCLUDED from Hash

```cpp
// During SER_GETHASH:
// - nFlags is NOT serialized
// - Hash computed from standard 80-byte header
// - This prevents nFlags from affecting block hash
// - Protects against hash malleability attacks
```

### Hash Serialization

```
Standard Bitcoin Header (80 bytes):
┌──────────────────────────────────────────────────────────────┐
│ nVersion (4) │ hashPrevBlock (32) │ hashMerkleRoot (32) │   │
├──────────────────────────────────────────────────────────────┤
│ nTime (4) │ nBits (4) │ nNonce (4) │                         │
└──────────────────────────────────────────────────────────────┘

Blackcoin More Network Header (84 bytes):
┌──────────────────────────────────────────────────────────────┐
│ nVersion (4) │ hashPrevBlock (32) │ hashMerkleRoot (32) │   │
├──────────────────────────────────────────────────────────────┤
│ nTime (4) │ nBits (4) │ nNonce (4) │ nFlags (4) │            │
└──────────────────────────────────────────────────────────────┘

Hash: Always computed from 80 bytes (nFlags excluded)
```

---

## nStakeModifier Field

### Definition (src/chain.h:225)

```cpp
//! CRITICAL: Blackcoin More PoS - hash modifier for proof-of-stake
//! This field is NOT present in Bitcoin Core and must NEVER be removed.
//! Used in stake kernel hash calculation (src/pos.cpp:CheckStakeKernelHash)
//! Computed during block validation (src/validation.cpp:2433)
//! Persisted to disk (src/node/blockstorage.cpp)
//! Bitcoin 30.x does NOT have this field - must be preserved as-is.
uint256 nStakeModifier{};
```

### Computation (src/pos.cpp:32)

```cpp
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel)
{
    if (!pindexPrev)
        return uint256();  // genesis block

    CHashWriter ss{};
    ss << kernel << pindexPrev->nStakeModifier;
    return ss.GetHash();
}
```

### Usage in Stake Kernel (src/pos.cpp:85)

```cpp
// PoS kernel formula:
// hash(nStakeModifier + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime) < bnTarget * nWeight
bool CheckStakeKernelHash(const CBlockIndex* pindexPrev, 
                          unsigned int nBits, 
                          uint32_t blockFromTime, 
                          CAmount prevoutValue, 
                          const COutPoint& prevout, 
                          unsigned int nTimeTx, 
                          bool fPrintProofOfStake)
{
    // Base target from difficulty
    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits);

    // Weighted target (more coins = higher chance)
    arith_uint256 bnWeight = arith_uint256(prevoutValue);
    bnTarget *= bnWeight;

    // CRITICAL: nStakeModifier from CBlockIndex
    uint256 nStakeModifier = pindexPrev->nStakeModifier;

    // Calculate proof-of-stake hash
    CHashWriter ss{};
    ss << nStakeModifier;
    ss << blockFromTime << prevout.hash << prevout.n << nTimeTx;

    uint256 hashProofOfStake = ss.GetHash();

    // Check if hash meets target
    return UintToArith256(hashProofOfStake) <= bnTarget;
}
```

### nStakeModifier Formula

```
stake_modifier_v2 = f(stake_modifier_v1, block_hash, staking_coin_timestamp)

Where:
- stake_modifier_v1 = previous block's modifier
- block_hash = current block's hash (or coinstake tx hash for PoS)
- staking_coin_timestamp = timestamp of the UTXO being staked

Purpose: Prevents precomputation of future stake hashes
```

---

## Serialization Flags (src/serialize.h)

```cpp
// src/serialize.h:128
SER_POSMARKER = (1 << 18)  // peercoin: for sending block headers 
                          // with PoS marker, to allow headers-first sync
```

### Flag Usage

| Flag | When Set | Effect |
|------|----------|--------|
| `SER_GETHASH` | Computing block hash | nFlags EXCLUDED from serialization |
| `SER_POSMARKER` | Network transmission | nFlags INCLUDED in serialization |
| `SER_NETWORK` | Network message | Includes SER_POSMARKER for PoS blocks |

---

## Network Message Flow

### Sending Block Header (src/netmessagemaker.h:23)

```cpp
int32_t serModes = nVersion <= OLD_VERSION 
    ? SER_NETWORK 
    : SER_NETWORK | SER_POSMARKER;  // Include nFlags for PoS blocks
```

### Receiving Block Header

```cpp
// src/net_processing.cpp
// Headers are deserialized with SER_POSMARKER
// nFlags is extracted and stored in CBlockIndex
```

### Block Disk Storage

```cpp
// src/node/blockstorage.cpp
// Full block (including vchBlockSig) persisted to disk
READWRITE(block);  // Includes vtx, vchBlockSig
```

---

## PoS Block Validation Checklist

### 1. Block Structure

```cpp
// src/validation.cpp
CValidationState state;
if (!CheckBlockHeader(block, state, Params().GetConsensus()))
    return false;

// Check PoS-specific requirements
if (block.IsProofOfStake()) {
    // Verify block signature
    if (!CheckBlockSignature(block))
        return state.Invalid(...);

    // Verify stake kernel
    if (!CheckProofOfStake(pindexPrev, block.vtx[1], block.nBits, state, view, block.nTime))
        return state.Invalid(...);
}
```

### 2. Serialization Rules

| Operation | Flags Used | nFlags | vchBlockSig | Hash |
|-----------|------------|--------|-------------|------|
| Compute hash | `SER_GETHASH` | ❌ Excluded | ❌ Excluded | ✅ 80 bytes |
| Network send | `SER_NETWORK \| SER_POSMARKER` | ✅ Included | ✅ Included | N/A |
| Network recv | `SER_NETWORK \| SER_POSMARKER` | ✅ Extracted | ✅ Extracted | N/A |
| Disk storage | Default | ✅ In CBlockIndex | ✅ In CBlock | N/A |

### 3. Block Index Persistence

```cpp
// src/chain.h - CBlockIndex fields persisted
READWRITE(obj.nFlags);           // BLOCK_PROOF_OF_STAKE flag
READWRITE(obj.nStakeModifier);   // Stake modifier (CRITICAL for PoS)
```

---

## Critical Upgrade Notes

### NEVER Port These Changes

| Feature | Why |
|---------|-----|
| Remove `nFlags` from `CBlockHeader` | PoS blocks won't be identifiable |
| Remove `nFlags` from `CBlockIndex` | Can't sync PoS headers |
| Remove `nStakeModifier` from `CBlockIndex` | PoS kernel validation breaks |
| Remove `vchBlockSig` from `CBlock` | Can't verify PoS block signatures |
| Change `SER_POSMARKER` behavior | Headers-first sync breaks |
| Remove `CheckBlockSignature()` | PoS blocks unverifiable |
| Remove `CheckStakeKernelHash()` | PoS kernel validation breaks |

### Verify After Upgrade

```bash
# Test PoS block creation
blackmore-cli staking true
blackmore-cli getstakinginfo

# Check block has correct structure
blackmore-cli getblock <blockhash> 2
# Verify "signature" field exists for PoS blocks
# Verify "nFlags" in verbose output

# Check stake modifier is preserved
blackmore-cli getblockchaininfo | grep stake
```

---

## Files Involved

| File | Role |
|------|------|
| `src/primitives/block.h` | CBlockHeader, CBlock structure definitions |
| `src/primitives/block.cpp` | GetHash(), ToString() implementation |
| `src/chain.h` | CBlockIndex with nFlags, nStakeModifier |
| `src/pos.cpp` | CheckStakeKernelHash(), ComputeStakeModifier() |
| `src/validation.cpp` | CheckBlockSignature(), block validation |
| `src/node/miner.cpp` | Block signing with vchBlockSig |
| `src/net_processing.cpp` | Headers sync with SER_POSMARKER |
| `src/blockencodings.cpp` | Compact block encoding with vchBlockSig |
| `src/serialize.h` | SER_POSMARKER flag definition |
| `src/kernel/chainparams.cpp` | Genesis block serialization |

---

## References

- **UPGRADE.md**: Bitcoin 26.x → 30.x upgrade plan
- **src/pos.cpp**: PoS kernel implementation
- **src/primitives/block.h**: Block structure definitions
- **src/chain.h**: CBlockIndex with PoS fields
- **Bitcoin Wiki - Proof of Stake**: https://en.bitcoin.it/wiki/Proof_of_Stake

---

*Generated for Blackcoin More Bitcoin 30.x Upgrade Project*
*See UPGRADE.md for complete upgrade plan*
