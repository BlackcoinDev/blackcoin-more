# Blackcoin More v27.2.0+: Fee & RBF Analysis

**Last Updated:** March 2026
**SegWit Status:** ✅ ACTIVATED on mainnet (height 5805000)

This document confirms that **Dynamic Fee Estimation** and **Replace-By-Fee (RBF)** are **DISABLED** in the codebase, and **Static Fees** are enforced via consensus and policy rules, aligning with Blackcoin's economic model.

## 1. Static Fees
Blackcoin uses a hardcoded static fee of **0.001 BLK per kB** (100,000 satoshis).

### Implementation Details
- **Constant Definition**: `TX_FEE_PER_KB` is defined as `100000` in `src/validation.h`.
- **Enforcement**: 
  - `src/consensus/tx_verify.cpp` (`GetMinFee`) uses `TX_FEE_PER_KB` as the base rate when Protocol v3.1 is active.
  - `src/policy/policy.h` sets `DEFAULT_BLOCK_MIN_TX_FEE`, `DUST_RELAY_TX_FEE`, and `DEFAULT_MIN_RELAY_TX_FEE` all to `100000`.
- **Wallet Logic**:
  - `src/wallet/fees.cpp` (`GetMinimumFeeRate`) explicitly clamps the fee to `TX_FEE_PER_KB` if the calculated fee is lower (for v3.1).

## 2. Dynamic Fee Estimation
The Bitcoin Core dynamic fee estimator (`CBlockPolicyEstimator`) is **completely disabled**.

### Status
- **Disabled**: usage in `src/init.cpp` is commented out.
- **Evidence**:
  ```cpp
  /*
  // Blackcoin
  assert(!node.fee_estimator);
  // ...
  // node.fee_estimator = std::make_unique<CBlockPolicyEstimator>(...);
  */
  ```
- **Impact**: The node does not collect fee statistics, does not write `fee_estimates.dat`, and RPC calls relying on estimation will fallback to static values or fail gracefully.

## 3. Replace-By-Fee (RBF)
RBF is **explicitly disabled** in the memory pool validation logic.

### Implementation Details
- **Location**: `src/validation.cpp` inside `MemPoolAccept::PreChecks`.
- **Logic**: The code checks for conflicting transactions but **immediately rejects** them with `TX_MEMPOOL_POLICY` ("txn-mempool-conflict") instead of evaluating replacement criteria.
- **Code Snippet**:
  ```cpp
  if (ptxConflicting) {
      // UPGRADE NOTE: RBF (Replace-By-Fee) is DISABLED in Blackcoin More
      // ...
      return state.Invalid(TxValidationResult::TX_MEMPOOL_POLICY, "txn-mempool-conflict");
  }
  ```
- **Result**: Users cannot replace transactions by bumping fees. The first seen transaction is always the one accepted (First-Seen-Safe).

## 4. Mempool & Coinstake Safety Verification

### Coinstake Fee Handling
**Question:** Do coinstake transactions cause fee validation issues?
**Feature Verified:** Coinstakes are **EXEMPT** from fee logic.
- **Code Location:** `src/consensus/tx_verify.cpp` inside `CheckTxInputs`.
- **Logic:** The minimum fee check is explicitly wrapped in `if (!tx.IsCoinStake())`.
  ```cpp
  if (!tx.IsCoinStake()) {
      // ... fee calculations ...
      if (Params().GetConsensus().IsProtocolV3_1(nTimeTx) && txfee_aux < GetMinFee(tx, nTimeTx))
          return state.Invalid(...);
  }
  ```
- **Conclusion:** Staking is unaffected by the static fee enforcement rules.

### Mempool Safety Check
**Question:** Does disabling the `FeeEstimator` cause crashes in the mempool?
**Feature Verified:** Mempool logic is decoupled from the fee estimator.
- **Dependency:** `MemPoolAccept` uses `IsCurrentForFeeEstimation`.
- **Implementation:** In `src/validation.cpp`, this function is hardcoded to return `true`:
  ```cpp
  static bool IsCurrentForFeeEstimation(const Chainstate& active_chainstate) { return true; }
  ```
- **Conclusion:** The mempool does not attempt to access the uninitialized `node.fee_estimator`, so there is no risk of null pointer dereferencing or missing data errors.

## Conclusion
The codebase correctly implements the user's requirement for static pricing and immutable transactions (no RBF). No further code changes are required to enforce these rules.
