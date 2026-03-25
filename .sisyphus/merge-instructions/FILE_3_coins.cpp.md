## FILE 3: src/coins.cpp

### Summary
Coin class implementation and UTXO set management. Handles coin serialization, deserialization, and cache operations. CRITICAL for PoS because it contains the fCoinStake flag and nTime field for PoS transaction tracking.

### Bitcoin 28.4.0 Changes
- **CTxOut serialization**: Improved serialization format
- **CCoinsMap improvements**: Better memory pool management
- **C++20 features**: Modern C++20 optimizations
- **Dynamic memory usage**: Better memory tracking

### Blackcoin Additions (MUST PRESERVE)
- **fCoinStake field**: Coin class has `fCoinStake : 1` bitfield
  - Line 47 in Blackcoin version
  - Marks coin as from a coinstake transaction
- **nTime field**: Coin class has `nTime` field for timestamp
  - Line 48 in Blackcoin version
  - Stores transaction timestamp for PoS kernel validation
- **Coin constructor with fCoinStake**: Lines 56-57
  ```cpp
  Coin(CTxOut&& outIn, int nHeightIn, bool fCoinBaseIn, bool fCoinStakeIn, int nTimeIn)
  ```
- **Coin serialization with fCoinStake**: Lines 86-98
  ```cpp
  uint32_t code = nHeight * uint32_t{4} + (fCoinBase ? 1 : 0) + (fCoinStake ? 2 : 0);
  ```
- **TRACE6 with IsCoinStake**: Lines 101-107, 134-140
  - Logs staking activity for debugging

### Conflict Zones
- **Line ~120**: AddCoins() function
  - **Bitcoin**: Only passes `fCoinBase` to Coin constructor
  - **Blackcoin**: Passes both `fCoinBase` AND `fCoinStake` AND `nTime`
- **Line ~126**: Coin constructor call
  ```cpp
  // Blackcoin: Coin(tx.vout[i], nHeight, fCoinbase, fCoinstake, tx.nTime)
  // Bitcoin:   Coin(tx.vout[i], nHeight, fCoinbase)
  ```
- **Line ~122**: AddCoins() signature
  - **Blackcoin**: Has `fCoinstake = tx.IsCoinStake()` detection

### Merge Strategy
- **KEEP**: Entire Blackcoin PoS extension to Coin class
- **KEEP**: fCoinStake bitfield (CONSENSUS CRITICAL)
- **KEEP**: nTime field (CONSENSUS CRITICAL)
- **KEEP**: fCoinStake in serialization code (lines 86-98)
- **MERGE**: Bitcoin's C++20 optimizations where safe
- **REMOVE**: Nothing - all Blackcoin additions are PoS-critical

**Specific Instructions**:
1. Preserve fCoinStake bitfield in Coin class (47-48)
2. Preserve nTime field in Coin class (48)
3. Preserve constructor with fCoinStake + nTime parameters
4. Preserve fCoinStake serialization code (code = nHeight*4 + coinbase + coinstake*2)
5. **CRITICAL**: This file's Coin class is the foundation of PoS validation

### Risk Level: HIGH

**Why HIGH**:
- Coin class is the foundation of UTXO set
- fCoinStake flag determines if coin can be used for staking
- nTime field is critical for PoS kernel validation
- Serialize/deserialize roundtrip must preserve all fields
- Any loss breaks PoS functionality

### Dependencies
- `src/primitives/transaction.h` - IsCoinStake() method
- `src/coins.h` - Coin class header
- `src/validation.cpp` - Uses Coin with fCoinStake
- `src/index/coinstatsindex.cpp` - Uses coin with is_pos flag
- `src/wallet/wallet.cpp` - Staking logic that reads fCoinStake
