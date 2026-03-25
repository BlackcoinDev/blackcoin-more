## FILE 5: src/wallet/rpc/spend.cpp

### Summary
Wallet spend RPC implementation. Handles sendtoaddress, sendmany, and transaction creation RPC calls. Must handle PoS-specific fee calculations and staking balance checks.

### Bitcoin 28.4.0 Changes
- **C++20 features**: Modern C++20 updates
- **Fee rate handling**: Improved fee rate parsing
- **Output subtract fee**: Better SFFO handling

### Blackcoin Additions (MUST PRESERVE)
- **GetAdjustedTimeSeconds() usage**: Fee calculation uses GetAdjustedTimeSeconds()
- **Staking-only wallet check**: Lines 194-196
  ```cpp
  if (wallet.m_wallet_unlock_staking_only)
      throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet unlocked for staking only");
  ```
- **Static fee message**: Hardcoded "Minimum required fee" (100,000 sat/kvB)
- **Burn RPCs**: Blackcoin-specific burn and burnwallet RPCs
- **PoS staking balance**: Checks `bal.m_mine_stake` for burnwallet

### Conflict Zones
- **Line ~194-196**: Staking-only wallet validation
  - **Bitcoin**: Does NOT have this check
  - **Blackcoin**: Prevents staking-only wallet from sending transactions
- **Line ~207-210**: Fee reason in verbose mode
  - **Bitcoin**: Uses dynamic fee reason
  - **Blackcoin**: Hardcoded "Minimum required fee"
- **Line ~319-320**: Burnwallet stake balance check
  ```cpp
  if (bal.m_mine_stake != 0)
      throw JSONRPCError(RPC_WALLET_ERROR, "Warning: stake balance != 0");
  ```

### Merge Strategy
- **KEEP**: All Blackcoin PoS-specific wallet checks
- **KEEP**: Staking-only wallet validation
- **KEEP**: Static fee message (100,000 sat/kvB)
- **KEEP**: Burn wallet stake balance checks
- **MERGE**: Bitcoin's C++20 improvements where safe
- **REMOVE**: Nothing PoS-specific

**Specific Instructions**:
1. Preserve staking-only wallet check (wallet_unlock_staking_only)
2. Preserve static fee message for min relay fee
3. Preserve burnwallet stake balance checks
4. Keep GetAdjustedTimeSeconds() if used for fee calculation
5. **CRITICAL**: This file enforces Blackcoin's static fee structure

### Risk Level: MEDIUM

**Why MEDIUM**:
- Wallet RPC (not direct consensus)
- If broken, wallet cannot send transactions
- Fee enforcement important for network health
- Staking checks prevent invalid transactions

### Dependencies
- `src/consensus/tx_verify.cpp` - GetAdjustedTimeSeconds() usage
- `src/wallet/wallet.h` - CWallet class with staking flags
- `src/wallet/transaction.h` - IsCoinStake() for balance
- `src/wallet/fees.h` - Fee calculations
