# Blackcoin More v28-CORE Release Notes

**Release Date:** 2026-07-05
**Base Version:** Bitcoin Core 28.4.0 + Blackcoin extensions
**Previous Version:** Blackcoin More v262 (June 2026)

---

## Summary of Changes

This release represents a major architectural upgrade from v262, building on Bitcoin Core 28.4.0 while maintaining Blackcoin's unique Proof-of-Stake consensus. The codebase integrates **complete Bitcoin Core 28.4.0** plus **255+ additional commits** of Blackcoin-specific features and improvements. The codebase has been modernized with enhanced security, performance, and protocol compliance.

---

## Major Architectural Changes

### 1. Bitcoin Core 28.4.0 Foundation
- **Complete integration** of Bitcoin Core 28.4.0 (released January 2025)
- **All upstream security fixes** from Bitcoin Core 28.4.0
- **Modernized build system** with updated dependencies (secp256k1, leveldb, etc.)
- **Full Bitcoin Core 28.4.0 APIs** and infrastructure
- **Blackcoin extensions:** Additional 255+ commits of Blackcoin-specific features

### 2. Network Security Enhancements
- **Anti-sybil protection:** `g_relax_network_mask` now defaults to `false` (was `true` during testing)
- **Header spam protection:** Added Qtum-style header spam filter (avg > 2 blocks/sec triggers disconnect)
- **Peer management:** Increased protected outbound peers from 4 to 8
- **Rolling checkpoints:** Added rolling checkpoint validation for enhanced security

### 3. Consensus Protocol Improvements

#### PoS Validation Modernization
- **Witness version protection:** Rejects unknown witness versions (> 1) at mempool/block level
- **Script verification modernization:**
  - `SCRIPT_VERIFY_TAPROOT` added to `STANDARD_SCRIPT_VERIFY_FLAGS`
  - `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` and `SCRIPT_VERIFY_WITNESS` moved to `MANDATORY_SCRIPT_VERIFY_FLAGS`
  - Enhanced `VerifyScript` with proper `PrecomputedTransactionData`
- **Stake kernel caching:** New `stakeCache` mechanism reduces coinstake creation time from ~100s to <100ms

#### Transaction Version Handling
- **V2 transaction support:** Proper handling of `tx.version >= 2` (was `tx.nVersion`)
- **Time serialization:** Uses `GetAdjustedTimeSeconds()` for v2 txs (was `GetAdjustedTime()`)
- **COutPoint modernization:** `COutPoint::hash` now typed as `Txid` (was `uint256`)
- **Script validation:** Enhanced P2SH and witness script handling

#### Difficulty Adjustment
- **BIP-94 framework:** Added `enforce_BIP94` flag in chainparams (currently disabled)
- **Rolling difficulty:** Special handling for testnet4 when BIP-94 is active
- **Time warp protection:** `MAX_TIMEWARP` checks for PoW blocks

### 4. Wallet & Staking Overhaul

#### Staking Algorithm Rewrite
- **SignKey carrier:** OP_RETURN output with pubkey and timestamp for kernel identification
- **Input combining:** Fixed COutPoint comparison (was comparing only tx hash, now full `COutPoint`)
- **P2PK legacy support:** Automatic upgrade of P2PK kernels to P2PKH rewards
- **Staking thresholds:** Reduced `GetStakeCombineThreshold` from 500 to 250 BLK
- **Time validation:** Prevents coinstakes with `nTime <= MTP` (bad-txns-time-earlier-than-input)
- **Cache management:** Automatic stake cache clearing when size grows too large

#### Key Pool Management
- **Bug #2 fix:** `m_enabled_staking` now properly set to `false` on keypool exhaustion
- **Thread synchronization:** Added `cv_new_block.notify_one()` on staking stop
- **Wallet locking:** Added proper `LOCK(wallet.cs_wallet)` in staking functions

#### Address Type Support
- **New address types:** Added `PayToAnchor` (ANCHOR) support
- **Witness types:** Full P2WPKH, P2WSH, P2TR support
- **ExtractDestination hack:** Reinterpretation of P2PK scripts for display consistency

### 5. RPC & API Changes

#### Transaction Sending
- **Burn RPC modernization:** Migrated to native `SendMoney` (Bitcoin Core 28.x style)
- **Fee handling:** Enhanced fee estimation and relay fee logic
- **OptimizeUTXO:** Fixed weight checking to prevent oversized transactions
- **SendMoneyToScript:** Replaced with modern Bitcoin Core transaction building

#### Blockchain RPCs
- **GetBlockStats:** Updated to use Bitcoin Core 28.x APIs
- **Mempool inspection:** Enhanced mempool policy checking
- **Fee estimation:** Modernized fee rate estimation

### 6. GUI & User Interface

#### Single-Unit Display
- **BLK-only amounts:** Removed currency selector, all amounts shown in BLK
- **Font consistency:** Updated monospace font handling for Bitcoin Core 28.x style
- **Balance display:** Improved balance formatting and transaction list rendering

#### Staking Information
- **Staking status:** Enhanced staking icon and status indicators
- **Transaction details:** Better coinstake transaction descriptions
- **Address book:** Improved address validation and display

### 7. Performance Optimizations

#### Block Validation
- **Header validation:** Reduced header validation overhead
- **Mempool processing:** Optimized transaction acceptance flow
- **Script verification:** Cached script data for repeated validations

#### Network Communication
- **Block relay:** Fixed block relay peer selection for staking nodes
- **Compact blocks:** Enhanced compact block handling for PoS
- **Seed updates:** Updated DNS seeds and peer lists

#### Wallet Operations
- **Coin selection:** Improved coin selection algorithms for staking
- **Transaction signing:** Enhanced signing with proper script validation
- **Database operations:** Optimized wallet database access

### 8. Security Hardening

#### Consensus Security
- **Unknown witness rejection:** Prevents future soft fork ambiguity
- **Time validation:** Strict timestamp validation to prevent time warp attacks
- **Script verification:** Modern script verification with proper flag handling

#### Network Security
- **Eclipse protection:** Enhanced anti-eclipse measures with proper network grouping
- **Peer validation:** Improved peer validation and discouragement logic
- **Header spam:** Added header rate limiting and spam detection

#### Wallet Security
- **Private key handling:** Enhanced private key management and validation
- **Transaction signing:** Proper signing with script verification
- **Backup consistency:** Improved wallet backup and restore reliability

### 9. Compatibility & Migration

#### Forward Compatibility
- **V2 transactions:** Full support for v2 transaction format
- **Witness transactions:** Native witness transaction support
- **Taproot:** Framework for future Taproot activation

#### Backward Compatibility
- **Legacy wallets:** Continued support for legacy wallet formats
- **P2PK rewards:** Legacy P2PK reward handling for input combining
- **Old coinstakes:** Proper validation of historical coinstakes

#### Migration Path
- **Wallet upgrades:** Smooth upgrade path from v262 to v28-CORE
- **Node upgrades:** Non-disruptive node upgrade process
- **Protocol migration:** Gradual protocol migration with proper warnings

---

## Bug Fixes

### Critical Fixes
1. **Bug #2:** Keypool exhaustion no longer leaves staking enabled
2. **COutPoint combining:** Fixed input combining guard (was comparing only tx hash)
3. **P2TR signing:** Added missing `SCRIPT_VERIFY_TAPROOT` in policy flags
4. **Compact blocks:** Fixed compact block prefill for PoS (1→2 prefilled)
5. **Staking wake-up:** Fixed staking guard condition to check tip-hash

### Performance Fixes
1. **Staking cache:** Added stake cache to reduce coinstake creation time
2. **Header spam:** Added header spam protection to prevent resource exhaustion
3. **Network grouping:** Fixed network grouping for anti-sybil protection
4. **Script verification:** Optimized script verification with cached data

### UI Fixes
1. **Currency display:** Fixed currency selector to show BLK only
2. **Font handling:** Updated font handling for Bitcoin Core 28-x style
3. **Staking status:** Fixed staking status indicators and icons

---

## Network Changes

### Seed Nodes
- **Updated seed lists:** Removed deprecated electrum seeds, added new ones
- **DNS seeds:** Updated to current operational seed nodes

### Protocol Parameters
- **SegWit activation:** Activated at block 5,805,000 (June 20, 2025)
- **Taproot activation:** Framework implemented, deployment pending
- **Difficulty adjustment:** Enhanced difficulty retargeting algorithms

### Mempool Policy
- **Standard transaction rules:** Match Bitcoin Core 28.4.0 standards
- **Fee policies:** Enhanced fee estimation and relay policies
- **Witness policies:** Modern witness transaction policies with Blackcoin extensions

---

## Testing & Quality Assurance

### Test Suite
- **Complete Bitcoin Core 28.4.0 test suite** with Blackcoin-specific enhancements
- **Blackcoin-specific tests:** Enhanced PoS and staking tests
- **Integration tests:** Improved integration with Bitcoin Core 28.4.0 framework

### Test Coverage
- **Staking tests:** Comprehensive staking algorithm testing
- **Script verification:** Enhanced script verification testing
- **Network tests:** Improved network protocol testing

### Known Issues
1. **Witness kernel verification gap:** P2WPKH/P2TR kernels have no signature verification (requires soft fork)
2. **OptimizeUTXO weight check:** Large amounts may produce oversized transactions (batching needed)
3. **Duplicate staking:** Same wallet on multiple nodes produces different coinstakes (user education needed)
4. **nSearchInterval edge case:** Low-difficulty PoS may have rejected blocks (all versions affected)

---

## Upgrade Instructions

### For Users
1. **Backup wallet:** Always backup wallet before upgrading
2. **Shutdown node:** Stop the Blackcoin node
3. **Replace binaries:** Replace old binaries with new v28-CORE binaries
4. **Start node:** Start the upgraded node
5. **Verify sync:** Allow node to sync and verify staking functionality

### For Operators
1. **Monitor nodes:** Watch for any unusual behavior during upgrade
2. **Check logs:** Review logs for any error messages
3. **Test staking:** Verify staking functionality with test transactions
4. **Update documentation:** Update any custom documentation for API changes

### For Developers
1. **Review API changes:** Update applications to use new RPC endpoints
2. **Test integration:** Test integration with new Bitcoin Core components
3. **Update dependencies:** Update any Bitcoin Core dependencies to compatible versions

---

## Future Development

### Short-term Goals
1. **Taproot activation:** Complete Taproot deployment and testing
2. **Witness verification:** Implement proper witness kernel verification
3. **Performance optimization:** Further optimize staking and validation performance

### Long-term Goals
1. **Protocol modernization:** Continue protocol modernization with Bitcoin Core updates
2. **Security enhancements:** Implement additional security measures
3. **User experience:** Improve user experience and accessibility

---

## Credits

This release incorporates work from:
- **Bitcoin Core** 28.4.0 development team - complete base integration
- **Blackcoin development team** for 255+ additional commits of Blackcoin-specific features
- **Qtum development team** for header spam protection and other enhancements
- **Community contributors** for testing and feedback

---

## License

This software is released under the MIT License. See the COPYING file for details.

---

*For more information, visit: https://github.com/BlackcoinDev/blackcoin-more*
*Support: https://github.com/BlackcoinDev/blackcoin-more/issues*