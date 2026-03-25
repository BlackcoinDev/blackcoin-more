## FILE 1: src/validation.cpp

### Summary
Main validation logic for blocks and transactions. Contains consensus validation functions, block acceptance rules, and PoS block signature verification. Critical for maintaining blockchain integrity.

### Bitcoin 28.4.0 Changes
- **C++20 migration**: Updated to use modern C++20 features
- **Txid strict typing**: CTransactionId → uint256 (already done in Blackcoin 27.x)
- **CNetFingerprint**: Added network fingerprinting for peers
- **GetAdjustedTime removal**: Bitcoin 28.x removed GetAdjustedTime() in favor of GetTime()

### Blackcoin Additions (MUST PRESERVE)
- **GetAdjustedTime() wrapper**: Lines 172-180 in Blackcoin version
  ```cpp
  int64_t GetAdjustedTime() {
      return GetAdjustedTimeInternal();
  }
  ```
- **PoS block signature validation**: CheckBlockSignature() function
  - Verifies vchBlockSig in block header
  - Uses ECDSA verification with stake input key
- **fCoinStake tracking**: Coin class has IsCoinStake() flag
- **nStakeModifier preservation**: CBlockIndex has nStakeModifier field
- **nFlags field**: CBlockHeader has nFlags for PoS marker
- **BCLog::COINSTAKE logging**: Staking debug category

### Conflict Zones
- **Line ~340**: GetAdjustedTime() usage in Blackcoin vs Bitcoin's GetTime()
  - **Bitcoin 28.4.0**: Uses GetTime() from util/time.h
  - **Blackcoin More**: Requires GetAdjustedTime() for PoS kernel validation
- **Line ~2110**: Coin class serialization
  - **Bitcoin**: `fCoinBase` only
  - **Blackcoin**: `fCoinBase` + `fCoinStake` + `nTime`
- **CheckBlockSignature()**: Function exists ONLY in Blackcoin, not Bitcoin

### Merge Strategy
- **KEEP**: All Blackcoin PoS validation logic (lines after Bitcoin 27.2 changes)
- **KEEP**: GetAdjustedTime() wrapper function
- **KEEP**: CheckBlockSignature() validation
- **MERGE**: Bitcoin's new features where safe (C++20, net fingerprinting)
- **REMOVE**: Nothing - this is PoS validation, must preserve all Blackcoin-specific code

**Specific Instructions**:
1. Keep entire Blackcoin-specific PoS validation section
2. Keep GetAdjustedTime() call in validation.cpp and related functions
3. Keep CheckBlockSignature() and all PoS block checks
4. Copy any safe Bitcoin 28.x performance improvements (if non-consensus)
5. **CRITICAL**: Maintain fCoinStake + nTime in Coin class

### Risk Level: HIGH

**Why HIGH**:
- This is the heart of PoS validation
- Any mistake breaks consensus
- GetAdjustedTime() removal would break all PoS validation
- CheckBlockSignature() validation is critical for security
- nStakeModifier/nFlags fields are consensus-critical

### Dependencies
- `src/primitives/block.h` - CBlockHeader::nFlags, CBlock::vchBlockSig
- `src/pos.cpp` - PoS kernel validation calls CheckBlockSignature
- `src/chain.h` - CBlockIndex::nStakeModifier
- `src/coins.h` - Coin class with fCoinStake/nTime
- `src/consensus/tx_verify.cpp` - GetAdjustedTimeSeconds usage
- `src/wallet/rpc/transactions.cpp` - IsCoinStake RPC output
- `src/node/blockstorage.cpp` - LevelDB deserialization
