## FILE 8: src/rpc/blockchain.cpp

### Summary
Blockchain RPC implementation. Handles getblock, getblockheader, getblockhash, and other chain-related RPCs. CRITICAL for PoS because it must include SER_POSMARKER to output nFlags and vchBlockSig in RPC responses.

### Bitcoin 28.4.0 Changes
- **C++20 features**: Modern C++20 updates
- **Output improvements**: Better JSON response formatting
- **Performance**: Faster block retrieval

### Blackcoin Additions (MUST PRESERVE)
- **SER_POSMARKER in RPC output**: Lines 615, 786-787
  ```cpp
  ssBlock.SetType(SER_NETWORK | SER_POSMARKER); // BLACKCOIN-SPECIFIC
  ```
- **nFlags in block output**: Line 1080
  ```cpp
  ret.pushKV("coinstake", (bool)coin.fCoinStake);
  ```
- **PoS block output**: Includes PoS-specific fields in RPC
- **vchBlockSig in JSON**: Block signature output

### Conflict Zones
- **Line ~615**: Block serialization for getblock
  - **Bitcoin**: Standard serialization
  - **Blackcoin**: Uses SER_POSMARKER to include nFlags
- **Line ~786-787**: Another SER_POSMARKER usage
  - **Bitcoin**: No PoS marker
  - **Blackcoin**: Sets SER_POSMARKER for PoS fields
- **Line ~1080**: coin.fCoinStake in RPC output
  - **Bitcoin**: Does not have coinstake flag
  - **Blackcoin**: Outputs PoS marker in block details

### Merge Strategy
- **KEEP**: All SER_POSMARKER usages in RPC serialization
- **KEEP**: nFlags output to RPC
- **KEEP**: vchBlockSig output to RPC
- **KEEP**: coin.fCoinStake in RPC output
- **MERGE**: Bitcoin's C++20 improvements where safe
- **REMOVE**: Nothing - SER_POSMARKER is CRITICAL for PoS

**Specific Instructions**:
1. Preserve SER_POSMARKER in all RPC block serialization
2. Keep nFlags output to RPC
3. Keep vchBlockSig output to RPC
4. Keep fCoinStake in RPC output
5. **CRITICAL**: Without SER_POSMARKER, RPC loses PoS data

### Risk Level: HIGH

**Why HIGH**:
- RPC serialization of blocks
- SER_POSMARKER required to output nFlags and vchBlockSig
- Broken serialization = RPC cannot show PoS blocks correctly
- Affects wallet/staking UI that depends on RPC

### Dependencies
- `src/primitives/block.h` - CBlockHeader with nFlags
- `src/serialize.h` - SER_POSMARKER definition
- `src/core_write.cpp` - Block serialization
- `src/wallet/rpc/transactions.cpp` - RPC output consumers
