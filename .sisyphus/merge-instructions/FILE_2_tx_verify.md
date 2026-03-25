## FILE 2: src/consensus/tx_verify.cpp

### Summary
Transaction verification logic. Contains functions for checking transaction inputs, sequence locks, signature op counts, and maturity rules. Critical for transaction validation before block acceptance.

### Bitcoin 28.4.0 Changes
- **C++20 features**: Updated to use modern C++20 constructs
- **Txid strict typing**: Txid type changes (already migrated in Blackcoin 27.x)
- **Performance optimizations**: Better cache usage in tx verification

### Blackcoin Additions (MUST PRESERVE)
- **GetAdjustedTimeSeconds() usage**: Lines 184-189 in Blackcoin version
  ```cpp
  int64_t nTimeTx = tx.nTime;
  if (!nTimeTx && tx.nVersion >= 2)
      nTimeTx = GetAdjustedTimeSeconds();
  ```
- **PoS maturity check**: Lines 197-200
  ```cpp
  if ((coin.IsCoinBase() || coin.IsCoinStake()) && nSpendHeight - coin.nHeight < 
      (::Params().GetConsensus().IsProtocolV3_1(nTimeTx) ? 
       ::Params().GetConsensus().nCoinbaseMaturity : Params().nCoinbaseMaturity))
  ```
- **IsCoinStake() check**: Consensus validation for coinstake maturity
- **GetAdjustedTime() dependency**: Required for v2 transaction timestamp calculation

### Conflict Zones
- **Line ~184-189**: GetAdjustedTimeSeconds() call
  - **Bitcoin 28.4.0**: Does NOT have this - uses tx.nTime directly
  - **Blackcoin More**: REQUIRES GetAdjustedTimeSeconds() for v2 timestamp fallback
- **Line ~198**: Maturity check for IsCoinStake()
  - **Bitcoin**: Only checks coin.IsCoinBase()
  - **Blackcoin**: Checks both coin.IsCoinBase() AND coin.IsCoinStake()
- **Line ~214**: PoS-specific rule at end of CheckTxInputs
  ```cpp
  if (!tx.IsCoinStake()) {
      // Check minimum fee
  }
  ```

### Merge Strategy
- **KEEP**: GetAdjustedTimeSeconds() calls (CRITICAL for PoS)
- **KEEP**: IsCoinStake() in maturity check
- **KEEP**: PoS-specific fee check around IsCoinStake()
- **MERGE**: Bitcoin's C++20 performance optimizations (safe)
- **REMOVE**: Nothing consensus-critical - all Blackcoin additions are PoS-specific

**Specific Instructions**:
1. Preserve entire BlackcoinPoS section (lines after Bitcoin 27.2 changes)
2. Keep GetAdjustedTimeSeconds() wrapper for v2 transaction timestamps
3. Keep IsCoinStake() in maturity validation - this is PoS consensus
4. Copy Bitcoin's safe performance improvements where non-consensus
5. **CRITICAL**: Maintain GetAdjustedTimeSeconds() usage

### Risk Level: HIGH

**Why HIGH**:
- Maturity rules are consensus-critical
- GetAdjustedTimeSeconds() required for PoS timestamp validation
- IsCoinStake() check determines if stake can be spent
- Any error breaks PoS transaction validation

### Dependencies
- `src/validation.h` - GetAdjustedTimeSeconds() declaration
- `src/primitives/transaction.h` - IsCoinStake() method
- `src/coins.h` - Coin class with IsCoinStake() flag
- `src/consensus/params.h` - Consensus parameters (IsProtocolV3_1())
- `src/timedata.cpp` - GetAdjustedTime() implementation
