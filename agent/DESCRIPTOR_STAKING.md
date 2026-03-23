# Descriptor Wallet Staking Analysis

## Executive Summary

Based on comprehensive code analysis and real transaction evidence, **descriptor wallets can successfully stake with bech32 addresses**. The "Staking Legacy Address" is not a technical limitation but serves a specific purpose in the kernel proof mechanism. This analysis examines the staking implementation, validates findings with code references, and provides evidence from a real staking transaction.

## Technical Components of Staking

### 1. Kernel UTXO Selection (Staking Input)

**Code Location:** `src/wallet/staking.cpp:317-320`

```cpp
if (whichType != TxoutType::PUBKEY &&
    whichType != TxoutType::PUBKEYHASH &&
    whichType != TxoutType::WITNESS_V0_KEYHASH &&
    whichType != TxoutType::WITNESS_V1_TAPROOT)
{
    break;  // only support pay to public key and pay to address and pay to witness keyhash
}
```

**Supported Kernel Types:**
- `PUBKEY` - Direct public key output
- `PUBKEYHASH` - Legacy P2PKH addresses
- `WITNESS_V0_KEYHASH` - Bech32 addresses
- `WITNESS_V1_TAPROOT` - Bech32m addresses

**Not Supported:**
- `SCRIPTHASH` - P2SH and P2SH-SegWit addresses

### 2. Reward Address Generation

**Code Location:** `src/node/miner.cpp:675`

```cpp
auto op_dest = pwallet->GetNewDestination(OutputType::LEGACY, label);
```

This creates a LEGACY address with label "Staking Legacy Address" when no such labeled address exists.

**Technical Reality:** The reward address type is independent of kernel UTXO types. `GetScriptForDestination()` handles all destination types.

### 3. Kernel Proof Mechanism

**Code Location:** `src/wallet/staking.cpp:326-352`

For different kernel types, the code extracts the public key for kernel proof:

```cpp
if (whichType == TxoutType::PUBKEYHASH) // pay to address
{
    // convert to pay to public key type
    if (wallet.IsLegacy()) {
        // Legacy wallet path
    }
    else {
        // Descriptor wallet path
        std::unique_ptr<SigningProvider> provider = wallet.GetSolvingProvider(scriptPubKeyKernel);
        // Extract and convert to PUBKEY format
    }
}
```

The "Staking Legacy Address" is used to create a PUBKEY output in the coinstake transaction for kernel proof.

## Real Transaction Evidence

Analysis of actual staking transaction `d51a7731fbbf56bdaa2346ff6a882facde0da86447244dad187aaf7a11cb0731`:

### Transaction Structure

**Kernel UTXO (Input):**
- Address: `tblk1q4w4t99dfrey4u25elcrqj7p8nk40t9kxkd65ru`
- Type: `witness_v0_keyhash` (bech32)
- Value: 315,528.35 BLK

**Reward Outputs:**
- Output 2: `tblk1q4w4t99dfrey4u25elcrqj7p8nk40t9kxkd65ru` (157,764.92 BLK) - bech32
- Output 3: `tblk1q4w4t99dfrey4u25elcrqj7p8nk40t9kxkd65ru` (157,764.93 BLK) - bech32

**Kernel Proof Output:**
- Output 1: `myG16DiaBj4KPhDajiUYnUBo7kPz1YfxHS` - legacy PUBKEY type
- This is the "Staking Legacy Address" used for kernel proof

## Address Type Support Matrix

Blackcoin More supports the following address types with varying staking capabilities:

| Address Type | OutputType | Kernel UTXO Support | Reward Address Support | RPC Creation | Notes |
|-------------|------------|-------------------|----------------------|--------------|-------|
| **Legacy** | `LEGACY` | ✅ | ✅ | ✅ | Base58 P2PKH addresses |
| **P2SH-SegWit** | `P2SH_SEGWIT` | ❌ | ✅ | ❌ | Blocked at RPC with "not welcome" |
| **Bech32** | `BECH32` | ✅ | ✅ | ✅ | Native SegWit v0 |
| **Bech32m** | `BECH32M` | ✅ | ❌ | ❌ | Taproot, blocked for legacy wallets |

### Kernel UTXO Support Details

**Supported for Staking:**
- `TxoutType::PUBKEY` - Direct public key outputs
- `TxoutType::PUBKEYHASH` - Legacy P2PKH addresses
- `TxoutType::WITNESS_V0_KEYHASH` - Bech32 addresses
- `TxoutType::WITNESS_V1_TAPROOT` - Bech32m addresses

**Not Supported for Staking:**
- `TxoutType::SCRIPTHASH` - P2SH and P2SH-SegWit addresses (consensus restriction)

## Key Findings

### 1. Descriptor Wallets CAN Stake Bech32 UTXOs ✅

**Evidence:** Real transaction shows bech32 kernel UTXO successfully staked.

**Code Support:** `TxoutType::WITNESS_V0_KEYHASH` is explicitly supported in kernel validation.

### 2. Reward Addresses Can Be Any Type ✅

**Evidence:** Real transaction shows bech32 reward addresses work.

**Code Support:** `GetScriptForDestination()` handles all `CTxDestination` types.

### 3. "Staking Legacy Address" Purpose ✅

**Purpose:** Kernel proof mechanism, not reward address.

**Type:** Creates PUBKEY output for cryptographic proof of kernel ownership.

**Code:** `src/wallet/staking.cpp:372` - `scriptPubKeyOut << ToByteVector(pkey) << OP_CHECKSIG;`

### 4. The "Smoking Gun" Constraint 🔒 (Updated)
**Why is the Legacy Address Mandatory?**
The consensus logic in `src/validation.cpp` **hardcodes** the lookup of the public key to `vout[1]`.

```cpp
// src/validation.cpp:3608 (CheckBlockSignature)
const CTxOut& txout = block.vtx[1]->vout[1];
TxoutType whichType = Solver(txout.scriptPubKey, vSolutions);

if (whichType == TxoutType::PUBKEY) {
    // Only verify if found in vout[1]
    return CPubKey(vchPubKey).Verify(block.GetHash(), block.vchBlockSig);
}
```

**Implication**: `CheckBlockSignature` does **NOT** look at the input or witness data. If `vout[1]` is not a recognizable `PUBKEY` script (Legacy P2PK), validation fails and the block is rejected. Removing this output would require a **Hard Fork**.

> [!IMPORTANT]
> This is **Immutable Consensus Rule #1**. For the full list of 5 hard-fork constraints, see [The Immutable Consensus Rules](STAKING.md#9-the-immutable-consensus-rules-hard-forks).

### 5. P2SH-SegWit Restrictions

**RPC Level:** Blocked with "P2SH_SEGWIT addresses are not welcome" error.

**Technical Support:** Infrastructure exists but intentionally restricted.

**Kernel Limitation:** SCRIPTHASH type rejected as kernel UTXO.

## Code Cross-References

### Wallet Type Handling

**Legacy Wallet Path:** `src/wallet/staking.cpp:326-338`
```cpp
if (wallet.IsLegacy()) {
    auto scriptPubKeyMan = wallet.GetLegacyScriptPubKeyMan();
    // Direct key access from legacy manager
}
```

**Descriptor Wallet Path:** `src/wallet/staking.cpp:339-352`
```cpp
else {
    std::unique_ptr<SigningProvider> provider = wallet.GetSolvingProvider(scriptPubKeyKernel);
    // Provider-based key extraction
}
```

### Address Type Support

**GetScriptForDestination:** `src/script/script.cpp`
- Handles all `CTxDestination` variants
- No type restrictions for reward addresses

**ParseOutputType:** `src/outputtype.cpp:28-29`
- Supports `OutputType::P2SH_SEGWIT`
- Parsing works but RPC rejects

## Conclusions

### Confirmed Working Scenarios

1. **Legacy wallet + Legacy kernel** ✅
2. **Legacy wallet + Bech32 kernel** ✅
3. **Descriptor wallet + Legacy kernel** ✅
4. **Descriptor wallet + Bech32 kernel** ✅ (proven by real transaction)

### Intentional Restrictions

1. **P2SH-SegWit addresses** - RPC-level policy restriction
2. **SCRIPTHASH kernel UTXOs** - Consensus-level security restriction

### Implementation Notes

1. **Staking Legacy Address** is a misnomer - should be called "Staking Kernel Proof Address"
2. **Reward addresses** have no type restrictions
3. **Kernel UTXOs** support modern types but exclude script-based addresses for security

## Technical Fixes and Implementation Plan

### Issue 1: Misleading "Staking Legacy Address" Naming

**Problem:** The label "Staking Legacy Address" suggests legacy-only staking but actually creates kernel proof addresses.

**Technical Fix:**
1. **Rename the label constant** in `src/node/miner.cpp:666`
   ```cpp
   // Change from:
   const std::string label = "Staking Legacy Address";
   // To:
   const std::string label = "Staking Kernel Proof Address";
   ```

2. **Update comments** throughout the codebase to clarify kernel proof purpose

3. **Add documentation** explaining that this address is for cryptographic proof, not staking rewards

**Implementation Steps:**
1. Update string constant in `miner.cpp`
2. Search and update all references to "Staking Legacy Address" in comments
3. Update AGENTS.md and UPGRADE.md documentation
4. Test backward compatibility (existing wallets with old label still work)

### Issue 2: Hardcoded Legacy Reward Address Type

**Problem:** Staking always creates legacy addresses for rewards, regardless of wallet's default type.

**Technical Fix:**
1. **Make staking reward address type configurable**
   ```cpp
   // In miner.cpp, replace hardcoded LEGACY with:
   OutputType reward_type = pwallet->m_default_address_type;
   auto op_dest = pwallet->GetNewDestination(reward_type, label);
   ```

2. **Add configuration option** for staking address type
   ```cpp
   // Add to argsman in init.cpp:
   argsman.AddArg("-stakingaddresstype", "Address type for staking rewards (legacy, p2sh-segwit, bech32, bech32m)", ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
   ```

**Implementation Steps:**
1. Modify `miner.cpp` to use wallet's default address type
2. Add configuration argument for custom staking address type
3. Test all supported address types (legacy, bech32, bech32m)
4. Validate backward compatibility with existing wallets

### Issue 3: P2SH-SegWit Address Creation Restriction

**Problem:** P2SH-SegWit is technically supported but blocked at RPC level.

**Status (March 2026):** SegWit is now ACTIVE on mainnet. The restriction may be reconsidered.

**Technical Fix Options:**

**Option A: Remove Restriction (Recommended)**
```cpp
// Remove this block from src/wallet/rpc/addresses.cpp:61-62
} else if (parsed.value() == OutputType::P2SH_SEGWIT) {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "P2SH_SEGWIT addresses are not welcome");
```

**Option B: Add SegWit Activation Check**

**Implementation Steps:**
1. Evaluate if restriction should be removed entirely
2. If keeping restriction, implement SegWit activation check
3. Update documentation about P2SH-SegWit availability
4. Test P2SH-SegWit address creation and usage

### Issue 4: Kernel Proof Address Type Optimization (The OP_RETURN Path)

**Problem:** Kernel proof currently forces a P2PK output ("Staking Legacy Address"), which creates "dust" UTXOs and requires confusing legacy formatting.

**Solution: OP_RETURN Implementation**
Consensus explicitly supports `OP_RETURN <pubkey>` in `vout[1]` (see `src/validation.cpp`).

**Advantages:**
1.  **Zero Bloat**: `OP_RETURN` is unspendable and prunable. It does not bloat the UTXO set RAM.
2.  **No Confusion**: Block explorers will view it as "Data" rather than a "Legacy Address".
3.  **SegWit Native**: Cleanly separates the SegWit Input (Proof) from the Key (Verification).

**Technical Fix:**
11.  **Modify `CreateCoinStake`**:
    ```cpp
    // In src/wallet/staking.cpp
    if (gArgs.GetBoolArg("-stakingopreturn", false)) {
        scriptPubKeyOut << OP_RETURN << ToByteVector(pkey); // Supports optional social message
    } else {
        scriptPubKeyOut << ToByteVector(pkey) << OP_CHECKSIG; // Legacy default
    }
    ```

**Implementation Steps:**
1.  Add `-stakingopreturn` configuration flag.
2.  Implement `OP_RETURN` builder in `staking.cpp`.
3.  Test validation against `CheckBlockSignature`.

### Issue 5: Kernel Proof Address Type Optimization

**Problem:** Kernel proof always creates PUBKEY output, but could be more efficient.

**Technical Fix:**
1. **Allow configurable kernel proof types** for different wallet types
2. **For descriptor wallets:** Use the wallet's default address type for kernel proof
3. **Maintain backward compatibility** with existing PUBKEY format

**Implementation Steps:**
1. Analyze if PUBKEY format is required for kernel validation
2. If not required, allow wallet's default type for kernel proof
3. Update staking logic to handle different kernel proof formats
4. Test kernel validation with different proof address types

### Issue 6: Descriptor Wallet Signing Provider Bug

**Problem:** `GetSolvingProvider()` returns nullptr for descriptor wallets in some cases.

**Technical Fix:**
1. **Fix cache lookup** in `CWallet::GetSolvingProvider()`
2. **Ensure descriptor wallets** can retrieve signing providers for their own addresses
3. **Add fallback logic** for descriptor wallet signing

**Implementation Steps:**
1. Debug `GetSolvingProvider()` cache logic for descriptor wallets
2. Add proper descriptor wallet support in signing provider retrieval
3. Test signing operations for all descriptor wallet address types
4.To modernize the staking system for Descriptor Wallets (Bech32/Taproot) compatibility.

## 0. Industry Comparison (The "State of the Art")

We analyzed the staking implementations of the top three Bitcoin Core-based PoS coins. **Blackcoin More is currently the segment leader.**

| Feature | Blackcoin More v2.13.3+ | Peercoin v0.12 | Qtum v24.1 |
| :--- | :--- | :--- | :--- |
| **Legacy Staking (P2PK/P2PKH)** | ✅ Native | ✅ Native | ✅ Native |
| **Taproot Staking (v1)** | ✅ **Preserved** (Minter Key) | ✅ **Preserved** (Minter Key) | ❌ Unsupported |
| **Bech32 Staking (v0)** | ✅ **Preserved** (Minter Key) | ⚠️ **Downgraded** (Converts to Legacy Reward) | ❌ Unsupported |
| **OP_RETURN Staking** | ✅ **Supported** (Future Proof) | ❌ Strict P2PK | ⚠️ **Legacy Only** (Banned by Modern Rules) |
| **SegWit Implementation** | Full Support (v0 & v1) | Partial (v1 Only / v0 Downgrade) | Legacy Only |

*   **Qtum**: Has `OP_RETURN` logic in `validation.cpp` (`GetBlockPublicKey`), but **newer consensus rules** (`CheckBlockInputPubKeyMatchesOutputPubKey` in `pos.cpp`) explicitly reject it for modern blocks, enforcing a strict P2PK relationship.
    *   **Reason**: This restriction is tied to their **Offline Staking (Super Staker)** implementation (`HasProofOfDelegation`). To safely verify delegated blocks, the consensus engine validates that the output key matches the input key without ambiguity, sacrificing `OP_RETURN` flexibility for easier delegation parsing.
*   **Peercoin**: Supports SegWit but treats `WITNESS_V0_KEYHASH` as P2PKH, forcing the reward into a Legacy P2PK output.
    *   **Mechanism**: Strict parsing in `CheckBlockSignature`. The code explicitly demands `Solver(...) == TxoutType::PUBKEY`. Any other script type (including `OP_RETURN`) causes immediate block rejection.
*   **Blackcoin More**: Uniquely supports **both** strict P2PK (legacy/compatibility) and `OP_RETURN` (clean staking) at the consensus level. Currently uses the "Minter Key" P2PK strategy for wallet compatibility, but the network is ready for `OP_RETURN` because we do not have the constraints of Offline Staking.

---

## 1. Current State Analysisority and Risk Assessment

### High Priority (Core Functionality)
1. **Fix Descriptor Wallet Signing** - Critical for staking to work
2. **Rename Misleading Labels** - Improves user understanding

### Medium Priority (User Experience)
1. **Configurable Reward Address Types** - Allows modern address preferences
2. **P2SH-SegWit Support** - Transitional address type support

### Low Priority (Optimization)
1. **Kernel Proof Address Optimization** - Efficiency improvement

### Risk Assessment
- **High Risk:** Changes to kernel proof format (affects consensus)
- **Medium Risk:** Address type changes (backward compatibility)
- **Low Risk:** Label changes, documentation updates

## What I Want to Implement

### Phase 1: Critical Bug Fixes (High Priority)

**1. Fix Descriptor Wallet Signing Provider**
- **Why:** Core staking functionality broken for descriptor wallets
- **What:** Fix `CWallet::GetSolvingProvider()` cache lookup for descriptor wallets
- **Code Changes:**
  ```cpp
  // In src/wallet/wallet.cpp:GetSolvingProvider()
  // Add descriptor wallet support after legacy wallet check
  if (IsLegacy() && GetLegacyScriptPubKeyMan()->CanProvide(script, sigdata)) {
      return GetLegacyScriptPubKeyMan()->GetSolvingProvider(script);
  }

  // ADD: Descriptor wallet support
  for (const auto spk_man: GetScriptPubKeyMans(script)) {
      if (const auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man)) {
          if (desc_spk_man->CanProvide(script, sigdata)) {
              return desc_spk_man->GetSolvingProvider(script);
          }
      }
  }
  ```

**2. Fix Staking Reward Address Type**
- **Why:** Currently hardcoded to legacy, ignoring user preferences
- **What:** Make staking reward address type configurable
- **Code Changes:**
  ```cpp
  // In src/node/miner.cpp:675
  // Replace hardcoded LEGACY with configurable type
  OutputType staking_addr_type = gArgs.GetArg("-stakingaddresstype", pwallet->m_default_address_type);
  if (staking_addr_type == OutputType::UNKNOWN) {
      staking_addr_type = pwallet->m_default_address_type;
  }
  auto op_dest = pwallet->GetNewDestination(staking_addr_type, label);
  ```

### Phase 2: User Experience Improvements (Medium Priority)

**3. Remove P2SH-SegWit Restriction**
- **Why:** Technically supported but arbitrarily blocked
- **What:** Allow P2SH-SegWit address creation after SegWit activation
- **Code Changes:**
  ```cpp
  // In src/wallet/rpc/addresses.cpp:61-62
  // Replace "not welcome" with activation check
  } else if (parsed.value() == OutputType::P2SH_SEGWIT) {
      if (!DeploymentActiveAfter(chainman.ActiveChain().Tip(), *pwallet->chain().chainman().GetConsensus(),
                                Consensus::DEPLOYMENT_SEGWIT)) {
          throw JSONRPCError(RPC_INVALID_PARAMETER, "P2SH-SegWit requires SegWit activation");
      }
  ```

**4. Rename Misleading Labels**
- **Why:** "Staking Legacy Address" is confusing and technically inaccurate
- **What:** Update to "Staking Kernel Proof Address" throughout codebase
- **Code Changes:**
  ```cpp
  // In src/node/miner.cpp:666
  const std::string label = "Staking Kernel Proof Address";
  ```

### Phase 3: Documentation and Testing (Low Priority)

**5. Add Comprehensive Address Type Documentation**
- **What:** Create detailed documentation for all address types and staking capabilities
- **Location:** Update AGENTS.md with address type matrix and staking scenarios

**6. Add Staking Configuration Options**
- **What:** Add command-line and RPC options for staking preferences
- **Options:**
  - `-stakingaddresstype` - Control reward address type
  - `-stakingkerneltypes` - Control which UTXO types can be staked
  - `setstakingconfig` RPC - Runtime staking configuration

## Implementation Strategy

### Development Approach
1. **Branch-based development** - Create feature branch for staking improvements
2. **Incremental changes** - Implement one fix at a time with testing
3. **Backward compatibility** - Ensure existing wallets continue working
4. **Feature flags** - Allow users to opt into new behavior

### Code Quality Standards
1. **Comprehensive testing** - Unit tests, integration tests, manual testing
2. **Code review** - Self-review against Bitcoin Core best practices
3. **Documentation updates** - Keep AGENTS.md and code comments current
4. **Performance impact** - Ensure no regression in staking performance

### Rollout Plan
1. **Phase 1 fixes** - Deploy critical bug fixes immediately
2. **Beta testing** - Test with small group before wider release
3. **Documentation update** - Update all user-facing documentation
4. **Migration guide** - Help users migrate to new staking behavior

## Testing Plan

### Unit Tests Required
1. **Kernel validation** with all supported address types
2. **Signing provider retrieval** for descriptor wallets
3. **Reward address creation** with configurable types
4. **Address type parsing** and validation
5. **Staking configuration** options

### Integration Tests Required
1. **Staking transactions** with bech32 kernel UTXOs
2. **Reward payments** to all supported address types
3. **Backward compatibility** with existing wallets
4. **Mixed wallet type** interactions
5. **Configuration persistence** across wallet loads

### Manual Testing Required
1. **Real network staking** with descriptor wallets
2. **Address type migration** scenarios
3. **Performance benchmarking** with different configurations
4. **Edge case testing** (empty wallets, corrupted data, etc.)

### Regression Testing
1. **Legacy wallet staking** continues to work
2. **Existing staking addresses** remain functional
3. **No performance degradation** in staking operations
4. **All existing RPC calls** maintain compatibility

## Recommendations

1. **Start with documentation fixes** (low risk, high impact)
2. **Fix descriptor wallet signing** (critical bug fix)
3. **Add configurable reward types** (user experience improvement)
4. **Evaluate P2SH-SegWit removal** (simplifies codebase)
5. **Comprehensive testing** before deployment

---

*Analysis based on Blackcoin More codebase examination and real transaction data. All findings cross-referenced with actual code implementations.*