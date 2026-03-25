## FILE 6: src/wallet/rpc/transactions.cpp

### Summary
Wallet transaction RPC implementation. Handles listtransactions, listreceivedbyaddress, and other transaction display RPCs. Must properly identify and display PoS transactions (coinbase, coinstake).

### Bitcoin 28.4.0 Changes
- **C++20 features**: Modern C++20 updates
- **Transaction handling**: Better transaction classification
- **Output display**: Improved unspent output handling

### Blackcoin Additions (MUST PRESERVE)
- **IsCoinStake() check**: Lines 22-23, 94, 373, 772
  ```cpp
  if (wtx.IsCoinBase() || wtx.IsCoinStake())
      entry.pushKV("generated", true);
  ```
- **Generated flag for coinstake**: Labels coinstake as "generated"
- **Coinstake transaction detection**: RPC output includes coinstake flag
- **Stake balance tracking**: Properly identifies staked coins in listtransactions

### Conflict Zones
- **Line ~22-23**: Generated flag for coinstake
  - **Bitcoin**: Only sets "generated" for coinbase
  - **Blackcoin**: Sets "generated" for both coinbase AND coinstake
- **Line ~94**: IsCoinStake() check in entry handling
  - **Bitcoin**: Does not distinguish coinstake
  - **Blackcoin**: Uses IsCoinStake() for transaction classification
- **Line ~318**: Coinstake transaction handling
  ```cpp
  if (wtx.IsCoinStake() && listSent.size() > 0 && listReceived.size() > 0) {
      // Handle stake movement
  }
  ```
- **Line ~772**: Coinstake transaction in listsinceblock
  - **Bitcoin**: Does not have special coinstake handling
  - **Blackcoin**: Shows coinstake as received transaction

### Merge Strategy
- **KEEP**: All Blackcoin PoS transaction handling
- **KEEP**: IsCoinStake() checks for generated flag
- **KEEP**: Coinstake-specific transaction logic
- **MERGE**: Bitcoin's C++20 improvements where safe
- **REMOVE**: Nothing PoS-specific

**Specific Instructions**:
1. Preserve IsCoinStake() check for "generated" flag
2. Preserve coinstake-specific transaction handling
3. Keep special code for stake movement detection
4. Copy Bitcoin's safe performance improvements
5. **CRITICAL**: This file affects RPC output for staking users

### Risk Level: MEDIUM

**Why MEDIUM**:
- RPC output only (not consensus)
- If broken, staking users see incorrect RPC responses
- Stake balance estimates may be wrong
- Does not break chain consensus

### Dependencies
- `src/wallet/wallet.h` - CWalletTx with IsCoinStake()
- `src/wallet/transaction.h` - Transaction class extension
- `src/coins.h` - Coin class with fCoinStake
- `src/primitives/transaction.h` - IsCoinStake() method
