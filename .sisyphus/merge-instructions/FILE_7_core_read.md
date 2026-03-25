## FILE 7: src/core_read.cpp

### Summary
Transaction and block deserialization from hex. Contains DecodeHexTx and DecodeHexBlock functions. CRITICAL for PoS because it must correctly deserialize blocks with SER_POSMARKER to read nFlags and vchBlockSig.

### Bitcoin 28.4.0 Changes
- **C++20 features**: Modern C++20 updates
- **Serialization improvements**: Better DataStream handling
- **Error handling**: Improved deserialization errors

### Blackcoin Additions (MUST PRESERVE)
- **SER_POSMARKER serialization**: Lines 210, 226
  ```cpp
  ser_header.SetType(SER_NETWORK | SER_POSMARKER);
  ssBlock.SetType(SER_NETWORK | SER_POSMARKER);
  ```
- **nFlags extraction**: Block header deserialization includes nFlags
- **vchBlockSig extraction**: Block deserialization includes vchBlockSig
- **PoS block deserialization**: Must read PoS extension fields

### Conflict Zones
- **Line ~210**: CBlockHeader deserialization
  - **Bitcoin**: Only deserializes standard header fields
  - **Blackcoin**: Uses SER_POSMARKER to include nFlags
- **Line ~226**: CBlock deserialization
  - **Bitcoin**: Deserializes only vtx
  - **Blackcoin**: Deserializes vtx AND vchBlockSig with SER_POSMARKER
- **IsHex** validation: Must handle PoS block serialization flags

### Merge Strategy
- **KEEP**: SER_POSMARKER flag in all block deserialization
- **KEEP**: nFlags extraction from header
- **KEEP**: vchBlockSig extraction from block
- **MERGE**: Bitcoin's C++20 improvements where safe
- **REMOVE**: Nothing - SER_POSMARKER is CRITICAL for PoS

**Specific Instructions**:
1. Preserve SER_POSMARKER in all block deserialization
2. Keep nFlags field extraction
3. Keep vchBlockSig field extraction
4. Copy Bitcoin's safe improvements
5. **CRITICAL**: This file reads PoS blocks from disk/network - must preserve nFlags

### Risk Level: HIGH

**Why HIGH**:
- Deserialization of blocks from disk/network
- SER_POSMARKER required to read nFlags correctly
- Without SER_POSMARKER, nFlags = 0, PoS blocks fail validation
- Breaks blockchain sync if fixed incorrectly

### Dependencies
- `src/primitives/block.h` - CBlockHeader with nFlags
- `src/serialize.h` - SER_POSMARKER definition
- `src/validation.cpp` - Block validation after deserialization
- `src/net_processing.cpp` - Network block deserialization
