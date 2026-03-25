## FILE 4: src/index/coinstatsindex.cpp

### Summary
Coin statistics index implementation. Tracks UTXO set statistics including total amounts, output counts, and hash calculations. Used for efficient coin statistics queries without full chain reindex.

### Bitcoin 28.4.0 Changes
- **MuHash improvements**: Better muhash implementation
- **Performance optimizations**: Faster statistics calculations
- **C++20 features**: Modern C++20 constructs

### Blackcoin Additions (MUST PRESERVE)
- **is_pos parameter**: Line 116 passes `block.is_pos` to GetBlockSubsidy
  ```cpp
  const CAmount block_subsidy{GetBlockSubsidy(block.height, Params().GetConsensus(), block.is_pos)};
  ```
- **Coin constructor with PoS fields**: Lines 151-152
  ```cpp
  Coin coin{out, block.height, tx->IsCoinBase(), tx->IsCoinStake(), (int)tx->nTime};
  ```
- **IsCoinStake() tracking**: Stores PoS coin info in index
- **nTime field**: Stores transaction timestamp for PoS validation

### Conflict Zones
- **Line ~151**: Coin instantiation
  - **Bitcoin**: `Coin{out, block.height, tx->IsCoinBase()}`
  - **Blackcoin**: `Coin{out, block.height, tx->IsCoinBase(), tx->IsCoinStake(), (int)tx->nTime}`
- **Line ~116**: GetBlockSubsidy() call
  - **Bitcoin**: 2 parameters
  - **Blackcoin**: 3 parameters (added `block.is_pos`)
- **Line ~426**: Second Coin instantiation in customAppend
  - **Bitcoin**: `Coin{out, pindex->nHeight, tx->IsCoinBase()}`
  - **Blackcoin**: `Coin{out, pindex->nHeight, tx->IsCoinBase(), tx->IsCoinStake(), (int)tx->nTime}`

### Merge Strategy
- **KEEP**: All Blackcoin PoS extensions to Coin creation
- **KEEP**: block.is_pos parameter to GetBlockSubsidy()
- **KEEP**: IsCoinStake() in Coin constructor
- **KEEP**: nTime field in Coin constructor
- **MERGE**: Bitcoin's performance optimizations where safe
- **REMOVE**: Nothing - all PoS-specific

**Specific Instructions**:
1. Preserve is_pos parameter in GetBlockSubsidy()
2. Preserve IsCoinStake() and nTime in all Coin constructors
3. Copy Bitcoin's safe performance improvements
4. **CRITICAL**: This file indexes PoS coins - must track IsCoinStake()

### Risk Level: MEDIUM

**Why MEDIUM**:
- Index file (not consensus-critical)
- If broken, needs reindex but chain continues
- IsCoinStake() tracking important for staking statistics
- nTime field less critical than in validation.cpp

### Dependencies
- `src/coins.h` - Coin class with PoS fields
- `src/primitives/transaction.h` - IsCoinStake() method
- `src/consensus/amount.h` - GetBlockSubsidy() with is_pos
- `src/kernel/coinstats.h` - CCoinsStats interface
