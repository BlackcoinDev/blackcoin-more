## FILE 9: src/node/blockstorage.cpp

### Summary
Block file storage and loading. Handles reading blocks from disk, block index loading, and LevelDB operations. CRITICAL for PoS because it must deserialize nFlags, nStakeModifier, and other PoS-specific fields from disk.

### Bitcoin 28.4.0 Changes
- **C++20 features**: Modern C++20 updates
- **Performance**: Better disk I/O handling
- **Memory management**: Improved block manager memory usage

### Blackcoin Additions (MUST PRESERVE)
- **nFlags deserialization**: Lines 140-141
  ```cpp
  pindexNew->nFlags         = diskindex.nFlags;
  pindexNew->nStakeModifier = diskindex.nStakeModifier;
  ```
- **nStakeModifier field**: CRITICAL for PoS kernel hash
- **PoS block index loading**: Must load all PoS fields from disk
- **CDiskBlockIndex extension**: Disk block index with PoS fields

### Conflict Zones
- **Line ~140-141**: Block index deserialization
  - **Bitcoin**: Only loads standard header fields (nVersion, hash, etc.)
  - **Blackcoin**: Loads nFlags and nStakeModifier from disk
- **Line ~136-141**: Block index load
  - **Bitcoin**: No PoS fields
  - **Blackcoin**: Loads PoS extension fields from CDiskBlockIndex
- **Line ~88-89**: Write batch for block index
  - **Blackcoin**: Must serialize nFlags and nStakeModifier to disk

### Merge Strategy
- **KEEP**: nFlags deserialization from disk
- **KEEP**: nStakeModifier deserialization from disk
- **KEEP**: All PoS extension fields in CDiskBlockIndex
- **MERGE**: Bitcoin's C++20 improvements where safe
- **REMOVE**: Nothing - PoS fields are CRITICAL for chain

**Specific Instructions**:
1. Preserve nFlags deserialization from disk
2. Preserve nStakeModifier deserialization from disk
3. Keep CDiskBlockIndex PoS extensions
4. Copy Bitcoin's safe performance improvements
5. **CRITICAL**: Without these, chain cannot sync from disk

### Risk Level: CRITICAL

**Why CRITICAL**:
- Block index loading from disk
- nStakeModifier required for PoS kernel validation
- No nStakeModifier = cannot validate PoS blocks
- Breaks chain resync if fixed incorrectly

### Dependencies
- `src/primitives/block.h` - Block header with PoS fields
- `src/chain.h` - CBlockIndex with nStakeModifier
- `src/validation.cpp` - Validation after block loading
- `src/undo.h` - Block undo data with PoS fields
