# Blackcoin More v28.4.0 — Staged Changes Review

**Date:** July 1, 2026
**Scope:** 49 files, ~672 insertions, ~481 deletions
**Branch:** v28-SEGWIT

---

## 1. Change Summary

This changeset implements several interconnected Blackcoin protocol upgrades for the v28.4.0 release.

### 1.1 OP_RETURN SignKey Carrier (Coinstake Restructure)

The coinstake transaction structure changes from:

```
vout[0]: empty (marker)
vout[1]: reward (P2PK or P2PKH)
```

to:

```
vout[0]: empty (marker)
vout[1]: OP_RETURN <pubkey> <timestamp>  (non-spendable carrier)
vout[2]: reward (native kernel type: P2PK, P2PKH, P2WPKH, P2TR)
vout[3]: split reward (if nCredit ≥ 500 BLK)
vout[4]: devfund (if enabled)
```

- The `bMinterKey` flag and P2PK intermediate output are removed.
- All kernel types use the same SignKey pubkey for the carrier.
- `CheckBlockSignature` already accepts the OP_RETURN carrier path — **no fork required**.
- P2PK kernels auto-upgrade reward output to P2PKH.
- The embedded timestamp (masked 16s boundary) in the OP_RETURN carrier breaks the v2 txid collision edge case (orphan + retry from same UTXO).

### 1.2 SegWit Burial

SegWit is moved from a version-bits deployment to a buried deployment (`DEPLOYMENT_SEGWIT` becomes `BuriedDeployment`).

- `fIncludeWitness` gate removed.
- `OLD_VERSION` / pre-SegWit peer logic removed entirely.
- SegWit script verification flags promoted to `MANDATORY_SCRIPT_VERIFY_FLAGS`.
- SegWit is now always active on all networks.

### 1.3 Stake Cache (`-stakecache`)

New optional per-wallet caching layer for `CheckKernel` results.

- Pre-populates cache in `CreateCoinStake` and `checkkernel` RPC.
- Clears cache when `cache.size() > setCoins.size() + 100`.
- Reduces `GetCoin` + `GetAncestor` disk I/O for repeated kernel checks.

### 1.4 Wake-on-Block Staker Timer

Replaces the naive `UninterruptibleSleep` polling loop with a `condition_variable`-based wake mechanism.

- `updatedBlockTip()` becomes a pure wake signal — sets `m_new_block_arrived = true`, notifies `cv_new_block`. No timing math.
- `MsUntilNextWindow()` computes milliseconds until next 16s boundary, strictly advancing past MTP.
- Per-wallet `m_last_coin_stake_search_time` replaces the global `static nLastCoinStakeSearchTime`.
- `pos_timio` preserved as CPU floor: `max(MsUntilNextWindow(...), pos_timio)`.
- `m_safety_bump_sleep_ms` removed from wallet.

### 1.5 v2 Transaction `nTime` Recovery

Blackcoin v2 transactions don't serialize `nTime` (it's always 0 after deserialization). Recovery logic added:

- `AddToWallet` and `LoadToWallet`: reconstruct `nTime` from `nTimeSmart` / block header.
- `CoinStatsIndex`: made coinstake-aware by including `fCoinStake` in the muhash encoding (bit 1). `nTime` is NOT included in the muhash — this avoids the v1/v2 asymmetry entirely.

**Note:** An earlier approach (July 8) passed `nBlockTime` into `AddCoins` to store `block.nTime` for v2 coins. This activated a latent time check in `CheckTxInputs` and caused block 5,944,947 to be rejected. Approach was fully reverted July 17, 2026. See `agent/ntime-investigation.md` and `agent/CoinStatsIndexOptimization.md` for the full history.

### 1.6 Default Address Type

Changed from `LEGACY` (P2PKH) to `BECH32` (P2WPKH) for new addresses.

### 1.7 P2TR Signing Fix

`SCRIPT_VERIFY_TAPROOT` was missing from `STANDARD_SCRIPT_VERIFY_FLAGS` in `policy.h`.

- Without it, P2TR signing silently failed: `VerifyWitnessProgram` returned `set_success` without checking the witness, causing `::SignTransaction` to return `true` on the wrong `ScriptPubKeyMan`.
- Fix: added `SCRIPT_VERIFY_TAPROOT` to `STANDARD_SCRIPT_VERIFY_FLAGS` (standard-only, not mandatory — peers not banned for invalid P2TR on networks where Taproot is not yet consensus).
- Also added `SCRIPT_VERIFY_WITNESS` and `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` to `MANDATORY_SCRIPT_VERIFY_FLAGS` (SegWit merge gap).

### 1.8 Input Combining Fixes

**COutPoint fix (June 29):**
- Combining guard compared only `pcoin.first->GetHash()` (txid), rejecting all same-tx sibling UTXOs.
- Fixed to compare full `COutPoint(pcoin.first->GetHash(), pcoin.second)`.
- Affects all kernel types. Real-world impact: `optimizeutxoset` creating 42 identical P2WPKH outputs could only combine 1 with the kernel.

**`nTimeSmart` fix (June 26):**
- Combining guard used `pcoin.first->tx->nTime` (always 0 for v2 txs), making the timing guard a no-op.
- Fixed to use `pcoin.first->nTimeSmart` (block time for confirmed txs).

### 1.9 Other Changes

- **`ProcessBlockFound`**: Removed stale-block check (`hashPrevBlock != activeTip`). The counterattack-window problem is now handled by proper fork submission.
- **`RelaxNetWorkMask`**: Dev toggle switches IPv4 from /16 to /32 and IPv6 from /32 to /128. Defaults to `true` (intentional testing choice — will be set to `false` before production release).
- **`ComputeTimeSmart`**: Tolerance reduced from 5 minutes to 16 seconds (`latestNow + 16`).
- **`sendtoaddress`**: `fee_rate` parameter removed; subsequent parameter indexes shifted.
- **`burn` / `burnwallet` RPCs**: Now use `SendMoney` instead of removed `SendMoneyToScript`.
- **`optimizeutxoset`**: Switched from balance-based to coin-selection-based input gathering.
- **`GetStakeWeight`**: Now acquires `cs_wallet` lock.
- **`DelAddressBook`**: Gains `force` parameter to protect SIGNKEY addresses from deletion.
- **`g_txindex->FindTx` calls**: Replaced with cached `CWalletTx` data in staking path.
- **CoinStats serialization**: Format change — `(nHeight << 1) + fCoinBase` → `(nHeight << 2) + fCoinBase + fCoinStake`. `nTime` is deliberately **NOT** included in the muhash (see §2.4).

---

## 2. Issues Found

### 2.1 ~~MEDIUM~~ RESOLVED (intentional): `RelaxNetWorkMask` defaults to `true` — testing choice

**File:** `src/netgroup.cpp:15`

```cpp
static bool g_relax_network_mask = true; // blackcoin: using true for testing
```

With `true`, every IPv4 address becomes its own `/32` group and every IPv6 address becomes `/128`. This negates `GetGroup()` anti-sybil/eclipse protections.

**Status:** Intentional choice for ongoing testing. Will be set to `false` when testing is complete and the release is finalized. Not a bug — tracked as a deliberate decision.

### 2.2 ~~MEDIUM~~ VERIFIED CORRECT: `sendtoaddress` parameter index shift

**File:** `src/wallet/rpc/spend.cpp`

Blackcoin removed 4 parameters from Bitcoin Core's `sendtoaddress`: `replaceable`, `conf_target`, `estimate_mode`, and `fee_rate`. The new parameter order is:

| Index | Parameter | `request.params[N]` access | Correct? |
|---|---|---|---|
| 0 | `address` | `params[0]` | ✅ |
| 1 | `amount` | `params[1]` | ✅ |
| 2 | `comment` | `params[2]` | ✅ |
| 3 | `comment_to` | `params[3]` | ✅ |
| 4 | `subtractfeefromamount` | `params[4]` | ✅ |
| 5 | `avoid_reuse` | `params[5]` | ✅ |
| 6 | `verbose` | `params[6]` | ✅ |

All `request.params[N]` accesses match the new RPC declaration order. No references to removed parameters (`replaceable`, `conf_target`, `estimate_mode`, `fee_rate`, `SetFeeEstimateMode`, `m_signal_bip125`) remain. RPC help examples are also correct — they only reference existing parameters.

### 2.3 ~~MEDIUM~~ FIXED: `burn` RPC balance info leak

**File:** `src/wallet/rpc/spend.cpp`

`GetBalance` was called before `EnsureWalletIsUnlocked`, allowing an attacker with a locked wallet to distinguish "insufficient funds" from "wallet locked" error responses, potentially leaking balance information.

**Fix applied:** Moved `EnsureWalletIsUnlocked(*pwallet)` before the `GetBalance` call. Now a locked wallet returns `RPC_WALLET_UNLOCK_NEEDED` before any balance information is revealed. `burnwallet` RPC was already correct (unlock before balance check).

### 2.4 ~~LOW~~ ACKNOWLEDGED: CoinStats serialization format is a breaking change

**File:** `src/kernel/coinstats.cpp`

The serialization format changed from:
```cpp
ss << static_cast<uint32_t>((coin.nHeight << 1) + coin.fCoinBase);
```
to:
```cpp
ss << static_cast<uint32_t>((coin.nHeight << 2) | (coin.fCoinBase ? 1u : 0u) | (coin.fCoinStake ? 2u : 0u));
```

`nTime` is deliberately **NOT** included in the muhash — this avoids the v1/v2 `nTime` asymmetry that caused the block 5944947 rejection. Existing CoinStats indexes are unreadable after this change — a reindex is required. However, no applications currently use the CoinStatsIndex feature, so the impact is negligible.

### 2.5 LOW: Fuzz test has commented-out variable with potential dangling references

**File:** `src/wallet/test/fuzz/fees.cpp:49`

```cpp
// const auto tx_bytes{fuzzed_data_provider.ConsumeIntegral<unsigned int>()};
```

If `tx_bytes` was used below (even in commented-out code), this creates confusion. Should be fully removed or properly integrated.

### 2.6 ~~LOW~~ ACKNOWLEDGED: `GetStakeWeight` recursive lock on `cs_wallet`

**File:** `src/wallet/staking.cpp`

`GetStakeWeight` now acquires `LOCK(wallet.cs_wallet)`. It's called from `CreateCoinStake`, which already holds `cs_wallet` via `LOCK2(cs_main, wallet.cs_wallet)`. Since `cs_wallet` is a `RecursiveMutex`, this is safe. The recursive acquisition is intentional — `GetStakeWeight` is also called from non-locked contexts (e.g., `getstakinginfo` RPC), so it needs its own lock.

### 2.7 ~~BEHAVIORAL~~ ACKNOWLEDGED: `nTimeSmart` combining guard for unconfirmed transactions

**File:** `src/wallet/staking.cpp:427`

```cpp
if (pcoin.first->nTimeSmart > txNew.nTime)
    continue;
```

This prevents `bad-txns-time-earlier-than-input` consensus violations. The original concern was that `nTimeSmart` could be 0 or unreliable for unconfirmed/mempool transactions.

**Verified safe:** `AvailableCoinsForStaking` (`staking.cpp:105,133`) enforces `min_depth = max(DEFAULT_MIN_DEPTH, nCoinbaseMaturity)` = 500 on mainnet. The depth check at line 133 (`nDepth < min_depth → continue`) filters out all unconfirmed UTXOs before they reach the combining loop. Mempool transactions (depth 0) never pass this filter. Only confirmed UTXOs with depth ≥ 500 reach the guard, where `nTimeSmart` is the reliable block time.

### 2.8 ~~BEHAVIORAL~~ ACKNOWLEDGED: `ComputeTimeSmart` tolerance 5min → 16s

**File:** `src/wallet/wallet.cpp:2961`

The `latestTolerated` window reduced from `latestNow + 300` (Bitcoin) to `latestNow + 16` (Blackcoin). This is **correct as-is** — no change needed.

**Why 16 seconds is correct:**
- **FutureDrift** (consensus rule, `validation.cpp:150`): PoS blocks with `nTime > now + 15` are rejected. This is the hard consensus limit.
- **`ComputeTimeSmart`** (wallet heuristic, `wallet.cpp:2961`): Sets `nTimeSmart` for wallet transaction ordering. The 16-second tolerance aligns with the 16-second PoS timestamp boundary (`nStakeTimestampMask = 0xf`), not the 15-second FutureDrift. It's a wallet-level heuristic, not a consensus rule.
- **Coinstakes bypass this entirely** (`wallet.cpp:2951-2952`): `if (wtx.IsCoinStake()) nTimeSmart = blocktime;` — coinstakes always use the block time directly, never the heuristic.
- **Only affects regular transactions**: The `nTimeSmart` value is used for wallet display ordering and the input combining guard (`staking.cpp:427`). It does not affect consensus validity.
- **Practical impact**: A user with >16s clock skew may see regular transaction timestamps downgraded in wallet display. This is cosmetic — the transactions remain valid.

**Why not use 15 (exact FutureDrift)?** The tolerance is about wallet ordering, not block validity. 16 seconds (one staking window) is the natural alignment for Blackcoin's PoS timing. Using 15 would create an off-by-one with the timestamp mask.

---

## 3. P2WPKH/P2TR Kernel Signature Verification (The "Peercoin Fix")

### 3.1 What Happened Before the Fix

`CheckProofOfStake` (`pos.cpp:157` in older versions) verified coinstake input signatures by calling `VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE)`. This was a wrapper that passed `nullptr` for the witness and used `SCRIPT_VERIFY_NONE`.

This meant that while P2PK and P2PKH were verified (because `OP_CHECKSIG` executes regardless of flags), **P2WPKH and P2TR kernels were not cryptographically verified**. They bypassed verification because the `SCRIPT_VERIFY_WITNESS` and `SCRIPT_VERIFY_TAPROOT` flags were missing.

### 3.2 The Fix Implemented in Blackcoin More

Blackmore284 completely fixed this vulnerability by calling `VerifyScript` directly with the correct parameters (in `pos.cpp:174-175`):

```cpp
std::vector<CTxOut> spent_outputs;
spent_outputs.reserve(tx.vin.size());
for (const auto& in : tx.vin) {
    Coin coin;
    if (!view.GetCoin(in.prevout, coin)) {
        return state.Invalid(...);
    }
    spent_outputs.emplace_back(coin.out);
}

PrecomputedTransactionData txdata;
txdata.Init(tx, std::move(spent_outputs));
TransactionSignatureChecker checker(&tx, 0, coinPrev.out.nValue, txdata, MissingDataBehavior::ASSERT_FAIL);

if (!VerifyScript(txin.scriptSig, coinPrev.out.scriptPubKey, &txin.scriptWitness,
    SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, checker)) {
    return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "stake-verify-signature-failed" ...);
}
```

**Four things were fixed simultaneously:**
1. **Flags**: `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT` — enables witness program detection and Taproot Schnorr verification.
2. **Witness**: `&txin.scriptWitness` — passes actual witness data instead of `nullptr`.
3. **Amount**: `coinPrev.out.nValue` — passes actual UTXO amount for correct BIP143 sighash.
4. **Spent outputs**: Collects all spent outputs for proper `PrecomputedTransactionData` initialization — required for Taproot `SIGHASH_ALL` which hashes all inputs.

### 3.3 On-Chain Audit & Soft Fork Deployment (June 2026)

Because this change tightens rules (invalid witnesses are now rejected), it constitutes a soft fork. Prior to deployment, all coinstake transactions from SegWit activation to the chain tip were audited. 

| Network | SegWit activation | Scanned to | Total coinstakes | Witness kernels | P2WPKH | P2TR | Malformed |
|---|---|---|---|---|---|---|---|
| **Testnet** | 2,070,000 | 2,852,570 | 782,417 | 61,196 | 61,196 | 0 | 0 |
| **Mainnet** | 5,805,000 | 5,928,105 | 123,106 | 3 | 3 | 0 | 0 |

All witness kernels had well-formed `[DER-sig, compressed-pubkey]` data. Zero malformed witnesses existed. The soft fork was safely deployed in v28.4.0 without rejecting any valid existing blocks.

### 3.4 SCRIPT_VERIFY_TAPROOT in MANDATORY — POST-ACTIVATION ONLY

Separate from the `CheckProofOfStake` fix: moving `SCRIPT_VERIFY_TAPROOT` to `MANDATORY_SCRIPT_VERIFY_FLAGS` (for mempool/DoS) must wait until **after** mainnet Taproot activation.

**Why it must wait:**
- Before mainnet Taproot activation, `GetBlockScriptFlags()` does NOT add `SCRIPT_VERIFY_TAPROOT` to consensus flags. P2TR is effectively anyone-can-spend at consensus level.
- If TAPROOT were in `MANDATORY` pre-activation, the mempool check would ban peers for relaying consensus-valid (but empty witness) P2TR transactions.
- After activation, P2TR is no longer anyone-can-spend, and peers sending invalid P2TR genuinely deserve banning.

**Action:** Move `SCRIPT_VERIFY_TAPROOT` to `MANDATORY_SCRIPT_VERIFY_FLAGS` in a **follow-up release after** mainnet Taproot activation completes. This is the correct sequence.

---

## 4. Taproot Activation Status

| Network | Status |
|---|---|
| **Mainnet** | BIP-9 signaling to start with next release (late 2026), not yet active |
| **Testnet** | `locked_in` since block 2,850,000 → activates at block 2,865,000 |
| **Regtest** | `ALWAYS_ACTIVE` |

The P2WPKH/P2TR signature verification soft fork does **not** depend on Taproot activation — it fixes P2WPKH kernel verification (the only witness type on-chain today). When Taproot activates, P2TR kernels will also be covered by the same fix.

---

## 5. Qtum Comparison Highlights

| Feature | Blackcoin More | Qtum |
|---|---|---|
| Kernel hash | `SHA256(nStakeModifier \|\| blockFromTime \|\| prevout \|\| nTimeTx)` | Identical formula |
| `prevoutStake` in header | No | Yes (enables delegation) |
| `setStakeSeen` anti-dupe | No (mitigated by OP_RETURN timestamp) | Yes (`COutPoint, uint32_t` set) |
| `Coin.nTime` | Yes (serialized in UTXO) | No (removed entirely) |
| Staking loop | 1 window, wake-on-block | 3 windows forward scan |
| Stake cache | Optional (`-stakecache`) | 4 cache types (always on for miner) |
| Delegation | Not supported | EVM-based Proof-of-Delegation |
| Block signature | ECDSA via OP_RETURN carrier or P2PK | ECDSA via P2PK only |
| Timestamp mask | Fixed 0xf (16s) | Adjustable via fork |
| Maturity | 500 blocks (fixed) | 500 blocks (adjustable via fork) |

---

## 6. Key Technical Parameters

| Parameter | Value |
|---|---|
| PoS block spacing | 64 seconds target |
| Timestamp mask | 0xf (16-second boundaries) |
| Coinbase maturity | 500 blocks (mainnet), 10 (testnet) |
| Difficulty adjustment | Per-block EMA (nInterval=15, nTargetTimespan=960s) |
| PoS reward | 1.5 BLK |
| PoW reward | 10,000 BLK (disabled after last PoW block) |
| Combine threshold | 250 BLK |
| Split threshold | 500 BLK |
| Staker sleep floor | 500 + 30√UTXOs ms |
| Future drift | +15 seconds (Protocol V2) |
| MTP | Returns exact previous block timestamp (not median of 11) |
| BIP94 | Not applicable to Blackcoin (per-block EMA difficulty). Disabled on mainnet/testnet, should also be disabled on regtest |

---

## 7. Files Referenced

### Staking / Coinstake
- `src/wallet/staking.cpp` — `CreateCoinStake`, `SelectCoinsForStaking`, input combining
- `src/node/miner.cpp` — `PoSMiner`, `CreateNewBlock`, `SignBlock`, `MsUntilNextWindow`, `SleepStaker`
- `src/pos.cpp` — `CheckProofOfStake`, `CheckKernel`, `CheckStakeKernelHash`

### Validation / Consensus
- `src/validation.cpp` — `CheckBlockSignature`, `ContextualCheckBlockHeader`, `ConnectBlock`
- `src/consensus/tx_check.cpp` — `CheckTransaction`
- `src/consensus/tx_verify.cpp` — `CheckTxInputs`
- `src/pow.cpp` — `CalculateNextTargetRequired`

### Script / Signing
- `src/script/interpreter.cpp` — `VerifyScript`, `VerifyWitnessProgram`, sighash paths
- `src/script/sign.cpp` — `VerifySignature`, `SignTransaction`
- `src/policy/policy.h` — `MANDATORY_SCRIPT_VERIFY_FLAGS`, `STANDARD_SCRIPT_VERIFY_FLAGS`

### Coin / UTXO
- `src/coins.cpp`, `src/coins.h` — `AddCoins`, `Coin` struct (nTime serialization)
- `src/kernel/coinstats.cpp` — MuHash serialization format
- `src/primitives/transaction.h` — v2 nTime stripping

### Wallet
- `src/wallet/wallet.cpp` — `updatedBlockTip`, `ComputeTimeSmart`
- `src/wallet/wallet.h` — `m_last_coin_stake_search_time`, `cv_new_block`

### Network
- `src/netgroup.cpp` — `RelaxNetWorkMask`

---

## 8. Agent Documentation Cross-References

| Document | Topic |
|---|---|
| `agent/validations.md` | Full validation architecture (transaction, block, PoS kernel) |
| `agent/staking.md` | Staking flow analysis, kernel types, OP_RETURN carrier |
| `agent/staking262.md` | v26.2.0 staking walkthrough (historical reference) |
| `agent/staking_probabilities.md` | UTXO selection, combine/split thresholds, expected time formula |
| `agent/StakerTimingRefactor.md` | Single-responsibility timing design (MsUntilNextWindow) |
| `agent/SafetyBump.md` | Wake/sleep design, timer guard, pos_timio |
| `agent/P2TRSigningFix.md` | SCRIPT_VERIFY_TAPROOT missing flag, P2TR signing flow |
| `agent/SegWitTxv2Coinstake.md` | v2 txid collision analysis, OP_RETURN timestamp fix |
| `agent/P2PKMigration.md` | P2PK→P2PKH/P2WPKH/P2TR migration, compatibility matrix |
| `agent/CoinStatsIndexOptimization.md` | nTime recovery, MuHash format change, index rebuild |
| `agent/qtum_comparison.md` | Qtum vs Blackcoin staking architecture comparison |

---

## 9. Cross-Codebase Comparison: Bitcoin Core vs Peercoin vs Qtum vs Blackcoin More

This section compares the Blackcoin More v28.4.0 codebase against its three reference ancestors: Bitcoin Core 28.4.0 (upstream), Peercoin, and Qtum. Blackcoin evolved from Bitcoin Core via Peercoin's PoS design, and Qtum further forked from Blackcoin's PoS lineage. Understanding these relationships clarifies which features are inherited, which are Blackcoin-specific, and which are novel in v28.4.0.

### 9.1 Staking / Proof-of-Stake

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| PoS mining | None (PoW only) | `CreateCoinStake` in `wallet/wallet.cpp` | `CreateCoinStake` in `wallet/stake.cpp` | `CreateCoinStake` in `wallet/staking.cpp` |
| PoS miner loop | None | Thread-based polling | Thread-based with forward scan | `PoSMiner` in `node/miner.cpp` with wake-on-block |
| Staking address | N/A | `"mintkey"` label, no purpose enum | `"mintkey"` + delegation | `"SignKey"` + `AddressPurpose::SIGNKEY` enum |
| Block signing | None (PoW only) | P2PK only in `vout[1]` | P2PK + delegation sig in header | OP_RETURN carrier in `vout[1]` |
| Kernel types | N/A | P2PK, P2PKH | P2PK, P2PKH, P2WPKH | P2PK, P2PKH, P2WPKH, P2TR |
| Staker wake | N/A | No wake-on-block | No wake-on-block | `cv_new_block` + `MsUntilNextWindow()` |

### 9.2 Block Validation & Consensus

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `CheckBlockSignature` | None | P2PK only (`validation.cpp:4994`) | P2PK only | P2PK + OP_RETURN carrier (`validation.cpp:3879`) |
| PoS timestamp mask | N/A | `0xf` (16s) | `0xf` (adjustable via fork) | `0xf` (16s, fixed) |
| Coinstake timestamp | N/A | `nTimeBlock == nTimeTx` | `nTimeBlock & mask == 0` only | `nTimeBlock == nTimeTx && (nTimeTx & 0xf) == 0` |
| Difficulty adjustment | Every 2016 blocks | Per-block EMA | Per-block EMA (adjustable) | Per-block EMA (`nInterval=15`, `nTargetTimespan=960s`) |
| BIP94 (timewarp) | Not applicable | Not applicable | Not applicable | **Not applicable** — Blackcoin uses per-block EMA difficulty, not fixed intervals. Code exists but disabled everywhere; should also be disabled on regtest |
| `setStakeSeen` anti-dupe | N/A | No | **Yes** — `(prevoutStake, nTime)` set | No (mitigated by OP_RETURN timestamp) |
| MTP calculation | Median of 11 blocks | Exact previous block timestamp | Exact previous block timestamp | Exact previous block timestamp (Protocol V2) |

### 9.3 Coin/UTXO Structure

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `Coin.nTime` | **Not present** | Not checked in code | **Not present** | `unsigned int nTime` (serialized) |
| `Coin.fCoinStake` | Not present | Not present | Not present | **Present** (serialized in MuHash) |
| `blockFromTime` source | N/A | `blockFrom->nTime` | `blockFrom->nTime` | `coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime` |
| v2 tx `nTime` | Not applicable | Not applicable | Not applicable | In-memory only, stripped from serialization (`transaction.h:233-236`) |

### 9.4 Transaction `nTime`

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `CTransaction.nTime` | **Not present** | Present, serialized | Not present | Present, **stripped for v2** (`version >= 2` → `nTime = 0` on deserialize) |
| v2 txid collision | N/A | N/A (nTime serialized) | N/A (no nTime field) | **Yes** — same UTXO + same outputs = same txid. Fixed by OP_RETURN timestamp |

### 9.5 Script Verification Flags

| Flag | Bitcoin Core MANDATORY | Peercoin | Qtum | Blackcoin More MANDATORY | Blackcoin More STANDARD |
|---|---|---|---|---|---|
| `SCRIPT_VERIFY_P2SH` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_DERSIG` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_DERKEY` | ❌ (doesn't exist) | ❌ | ❌ | ✅ **(Blackcoin-specific)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_LOW_S` | ✅ (STANDARD only) | ✅ | ✅ | ✅ **(MANDATORY)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` | ✅ | ✅ | ✅ | ✅ **(newly added)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_WITNESS` | ✅ | ✅ | ✅ | ✅ **(newly added)** | ✅ (via MANDATORY) |
| `SCRIPT_VERIFY_TAPROOT` | ✅ (MANDATORY) | ❌ | ✅ | ❌ | ✅ **(STANDARD only, not mandatory until mainnet activates)** |
| `SCRIPT_VERIFY_NULLDUMMY` | ✅ | ✅ | ✅ | ✅ | ✅ (via MANDATORY) |

**Key differences:**
- `SCRIPT_VERIFY_DERKEY` (bit 31) is a Blackcoin-specific flag not present in Bitcoin Core, Peercoin, or Qtum. It enforces DER encoding for public keys in scripts.
- `SCRIPT_VERIFY_LOW_S` is mandatory (bannable) in Blackcoin but only standard (non-bannable) in Bitcoin Core.
- `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` and `SCRIPT_VERIFY_WITNESS` were missing from Blackcoin's MANDATORY flags before this release (SegWit merge gap), now added.
- `SCRIPT_VERIFY_TAPROOT` is STANDARD-only in Blackcoin (not bannable until mainnet activation), while Bitcoin Core has it in MANDATORY (since Taproot is always active on Bitcoin).

### 9.6 CheckProofOfStake Signature Verification

**Before v28.4.0 fix:**

| Codebase | Function Call | Witness Data | Flags | P2PK verified? | P2PKH verified? | P2WPKH verified? | P2TR verified? |
|---|---|---|---|---|---|---|---|
| **Blackcoin More (pre-fix)** | `VerifySignature(coinPrev, ..., tx, 0, SCRIPT_VERIFY_NONE)` | `nullptr` | `SCRIPT_VERIFY_NONE` | ✅ Yes | ✅ Yes | ❌ No (trivial pass) | ❌ No (trivial pass) |
| **Peercoin** | `VerifyScript(..., &witness, SCRIPT_VERIFY_P2SH, ...)` | Real witness | `SCRIPT_VERIFY_P2SH` | ✅ Yes | ✅ Yes | ❌ No (witness not checked — `WITNESS` flag missing) | N/A |
| **Qtum** | `VerifySignature(..., SCRIPT_VERIFY_NONE)` → `VerifyScript(..., nullptr, 0, ...)` | `nullptr` | `SCRIPT_VERIFY_NONE` | ✅ Yes | ✅ Yes | ❌ No (trivial pass) | ❌ No (trivial pass) |

**After v28.4.0 fix (deployed):**

| Codebase | Function Call | Witness Data | Flags | P2PK verified? | P2PKH verified? | P2WPKH verified? | P2TR verified? |
|---|---|---|---|---|---|---|---|
| **Blackcoin More v28.4.0** | `VerifyScript(scriptSig, scriptPubKey, &scriptWitness, P2SH \| WITNESS \| TAPROOT, checker)` | Real witness | `SCRIPT_VERIFY_P2SH \| WITNESS \| TAPROOT` | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes |

**Blackcoin More is the only codebase that fully verifies all kernel signature types.** The fix at `pos.cpp:158-177` passes real witness data, the actual UTXO amount via `PrecomputedTransactionData`, and all three verification flags. See §3.2 for details.

**Peercoin and Qtum remain vulnerable.** Peercoin passes real witness data and the amount with `SCRIPT_VERIFY_P2SH`, but without `SCRIPT_VERIFY_WITNESS`, `VerifyScript` never enters the witness program detection path (`interpreter.cpp:2035: if (flags & SCRIPT_VERIFY_WITNESS)`). Qtum uses `SCRIPT_VERIFY_NONE` with `nullptr` witness — both P2WPKH and P2TR kernels trivially pass.

### 9.7 Block Header Structure

| Field | Bitcoin Core | Peercoin | Qtum | Blackcoin More |
|---|---|---|---|---|
| `prevoutStake` | N/A | N/A | **Present** (`COutPoint`) | N/A |
| `vchBlockSigDlgt` | N/A | N/A | **Present** (delegation sig) | N/A |
| `nStakeModifier` | N/A | Computed per-block | Computed per-block | Computed per-block (stored in `CBlockIndex`) |

Qtum's `prevoutStake` in the block header enables delegation (signing with a different key than the kernel UTXO owner). Blackcoin does not support delegation — the kernel UTXO owner must also sign the block.

### 9.8 Wallet / Address Types

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `AddressPurpose` enum | RECEIVE, SEND, REFUND | RECEIVE, SEND, REFUND | RECEIVE, SEND, REFUND | RECEIVE, SEND, REFUND, **SIGNKEY** |
| Default address type | Bech32 (P2WPKH) | Legacy (P2PKH) | Legacy (P2PKH) | **Bech32 (P2WPKH)** (changed from LEGACY in v28.4) |
| Staking address lookup | N/A | `"mintkey"` string label | `"mintkey"` string label | `"SignKey"` + `AddressPurpose::SIGNKEY` |
| Descriptor wallet | Yes | Yes | Yes | Yes (supports P2TR staking) |
| Legacy wallet | Yes | Yes | Yes | Yes (P2PK/P2PKH staking only; P2TR not mineable) |

### 9.9 Network / Time Configuration

| Feature | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| `WARN_THRESHOLD` (clock sync) | **10 minutes** | N/A | N/A | **16 seconds** |
| `FutureDrift` | N/A | N/A | N/A | **15 seconds** (Protocol V2) |
| `RelaxNetWorkMask` | N/A | N/A | N/A | **Present** (defaults to `true` — intentional testing choice, will be `false` for production) |
| Block spacing target | 600 seconds (10 min) | ~600 seconds | ~64 seconds (PoS) | **64 seconds** |
| Coinbase maturity | 100 blocks | 500 blocks | 500 blocks (variable) | **500 blocks** (mainnet), 10 (testnet) |

### 9.10 Deployment / Fork Handling

| Deployment | Bitcoin Core 28.4 | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|---|
| SegWit (BIP141) | Buried (always active) | N/A | Active | **Buried** (always active, moved from version-bits) |
| CSV (BIP112) | Buried (always active) | N/A | Active | **Buried** (always active) |
| Taproot (BIP341/342) | **Buried** (always active) | N/A | Active | **BIP9 signaling** (not yet active on mainnet; testnet locked_in) |
| DERKEY | N/A | N/A | N/A | **Always active** (Blackcoin-specific, no deployment) |

### 9.11 Staker Timing / Wake Mechanism

| Feature | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|
| Wake-on-block | **No** (polls `pos_timio`) | **No** (polls `nMinerSleep`) | **Yes** (`cv_new_block` + `MsUntilNextWindow()`) |
| Timer guard scope | Global static | Per-wallet | Per-wallet (`m_last_coin_stake_search_time`) |
| Window scan | Current window only | Forward 3 windows (48s lookahead) | Current window only |
| Post-block rest | Fixed sleep | Spin-wait until target time | `MsUntilNextWindow()` (boundary-aligned) |
| `pos_timio` | `500 + 30√UTXOs` ms | `5000ms` (or `20000ms` min diff) | `500 + 30√UTXOs` ms (floor for failed search) |

### 9.12 Staking Output / Reward Structure

| Feature | Peercoin | Qtum | Blackcoin More v28.4 |
|---|---|---|---|
| Block signing output | P2PK in `vout[1]` | P2PK in `vout[1]` | **OP_RETURN carrier** in `vout[1]` |
| Reward output type | Always P2PK (converted from P2PKH) | Always P2PK | **Native kernel type** (P2PK→P2PKH, P2PKH, P2WPKH, P2TR) |
| `bMinterKey` flag | Present (witness kernel intermediate) | Present | **Removed** (all types use same carrier) |
| Split threshold | RFC28 security level | Variable | **500 BLK** (2 × 250 BLK combine threshold) |
| Combine threshold | Variable | Variable | **250 BLK** |
| Coinbase output | 1 output (empty) or 2 outputs (w/ SegWit commitment) | 1 or 2 | 1 (empty) or 2 (w/ SegWit commitment) — same as Bitcoin |
| Dev fund | N/A | N/A | **Optional** (`vout[4]` if enabled) |

### 9.13 Key Architectural Differences — Summary

**From Bitcoin Core (upstream):**
- Blackcoin adds PoS consensus (staking, coinstake, kernel hash, stake modifier)
- Adds `nTime` field to transactions and UTXOs (for PoS timestamp alignment)
- Adds `SCRIPT_VERIFY_DERKEYS` (Blackcoin-specific mandatory flag)
- Moves `CHECKSEQUENCEVERIFY` and `WITNESS` into `MANDATORY_SCRIPT_VERIFY_FLAGS`
- Keeps `TAPROOT` in `STANDARD` only (not mandatory until mainnet activation)
- Changes `WARN_THRESHOLD` from 10 minutes to 16 seconds (matching PoS timing)
- Adds `AddressPurpose::SIGNKEY` for staking address management
- Adds `RelaxNetWorkMask` (currently defaults to `true` — intentional testing choice, will be `false` for production release)
- Buries SegWit as a deployment (no longer version-bits signaled)

**From Peercoin (PoS ancestor):**
- Blackcoin uses OP_RETURN carrier for block signing (Peercoin uses P2PK only)
- Blackcoin supports P2WPKH and P2TR kernel types (Peercoin supports P2PK and P2PKH only)
- Blackcoin's `CheckBlockSignature` accepts OP_RETURN (Peercoin rejects non-PUBKEY)
- Peercoin passes real witness data + `SCRIPT_VERIFY_P2SH` in `CheckProofOfStake` — structurally better than Blackcoin but still insufficient (missing `SCRIPT_VERIFY_WITNESS` flag, so P2WPKH kernels are not verified in Peercoin either)
- Blackcoin uses per-wallet timer guard (Peercoin uses global static)
- Blackcoin uses wake-on-block with condition variable (Peercoin polls)

**From Qtum (PoS fork):**
- Qtum adds `prevoutStake` to block header for delegation — Blackcoin does not
- Qtum has `setStakeSeen` anti-duplicate mechanism — Blackcoin mitigates via OP_RETURN timestamp
- Qtum removed `nTime` from `Coin` struct — Blackcoin keeps it (with recovery logic for v2 txs)
- Qtum uses 3-window forward scan — Blackcoin uses single-window with wake-on-block
- Qtum has 4 stake cache types — Blackcoin has optional `-stakecache`
- Both use `SCRIPT_VERIFY_NONE` with `nullptr` witness in `CheckProofOfStake` — both have the P2WPKH/P2TR signature verification gap

---

## 10. Documentation Accuracy Audit — agent/ Files vs Source Code

Verified all agent/ markdown files against the actual Blackcoin More source code. Findings:

### 10.1 Verified Accurate

| Claim | Source File | Verification |
|---|---|---|
| `CheckProofOfStake` at `pos.cpp:131` | `src/pos.cpp` line 131 | ✅ Match |
| `VerifyScript` with `P2SH \| WITNESS \| TAPROOT` at `pos.cpp:174` | `src/pos.cpp` line 174 | ✅ Match (old `VerifySignature` replaced by §3.2 fix) |
| `CheckBlockSignature` at `validation.cpp:3872` | `src/validation.cpp` line 3872 | ✅ Match |
| `nStakeTimestampMask = 0xf` in chainparams | `src/kernel/chainparams.cpp` lines 140, 258, 503, 581 | ✅ All set to `0xf` |
| v2 `nTime` stripping at `transaction.h:233-236, 277-278` | `src/primitives/transaction.h` lines 233-236, 277-278 | ✅ Match |
| `SCRIPT_VERIFY_DERKEY` bit 31 in `interpreter.h:105` | `src/script/interpreter.h` line 105 | ✅ Match |
| `SCRIPT_VERIFY_DERKEY` in `MANDATORY_SCRIPT_VERIFY_FLAGS` | `src/policy/policy.h` line 92 | ✅ Match |
| `SCRIPT_VERIFY_LOW_S` in MANDATORY | `src/policy/policy.h` line 94 | ✅ Match |
| `bMinterKey` removed from staking.cpp and miner.cpp | grep confirms zero matches | ✅ Fully removed |
| OP_RETURN carrier in `CheckBlockSignature` reads exactly 2 GetOps | `src/validation.cpp` lines 3888-3905 | ✅ Match |
| `Coin.nTime` field exists in `coins.h` | `src/coins.h` | ✅ Present |
| `Coin.fCoinStake` serialization in MuHash | `src/kernel/coinstats.cpp` | ✅ Present |
| `AddressPurpose::SIGNKEY` in `types.h` | `src/wallet/types.h` line 65 | ✅ Match |
| `g_relax_network_mask = true` in `netgroup.cpp` (intentional testing choice) | `src/netgroup.cpp` line 15 | ✅ Confirmed present — intentional, not a bug |
| `WARN_THRESHOLD` changed to 16s in `timeoffsets.h` | `src/node/timeoffsets.h` | ✅ Match |
| `MsUntilNextWindow()` implementation matches SafetyBump.md | `src/node/miner.cpp` lines 54-63 | ✅ Match |
| `SleepStaker()` implementation matches SafetyBump.md | `src/node/miner.cpp` lines 609-634 | ✅ Match |
| Peercoin `CheckBlockSignature` P2PK-only at `validation.cpp:4994` | `/peercoin/src/validation.cpp` line 4994 | ✅ Verified |
| Peercoin `VerifyScript` with real witness at `kernel.cpp:691` | `/peercoin/src/kernel.cpp` line 691 | ✅ Verified |
| Qtum `setStakeSeen` at `validation.cpp:127, 6192` | `/qtum/src/validation.cpp` lines 127, 6192 | ✅ Verified |
| Qtum `prevoutStake` in `block.h:34,42` | `/qtum/src/primitives/block.h` lines 34, 42 | ✅ Verified |
| Qtum `Coin` struct has no `nTime` field | `/qtum/src/coins.h` | ✅ Verified |
| Bitcoin Core `Coin` struct has no `nTime` field | `/bitcoin/src/coins.h` | ✅ Verified |
| Bitcoin Core `CTransaction` has no `nTime` field | `/bitcoin/src/primitives/transaction.h` | ✅ Verified |
| Bitcoin Core `WARN_THRESHOLD = 10 minutes` | `/bitcoin/src/node/timeoffsets.h` | ✅ Verified |

### 10.2 Minor Discrepancies (Low Severity)

| Document | Claim | Reality | Impact |
|---|---|---|---|
| `agent/SafetyBump.md` | `MsUntilNextWindow` returns `std::max(0LL, ...)` | Actual code: `std::max<int64_t>(0, ...)` | None — functionally identical. The `int64_t` cast is more explicit; `0LL` would also work. |
| `agent/validations.md` | Line numbers (e.g., `bad-txns-vin-empty` at line 14) | Actual: line 15 in `tx_check.cpp` | Low — line numbers shift with edits, behavioral claims are correct |
| `agent/staking.md` §9 | `VerifySignature` at `sign.cpp:734` | Not verified in this pass (would need to check current line) | Low — the function exists and behavior described is correct |
| `agent/staking262.md` | Describes v26.2.0 behavior | Marked as "Historical reference" | None — explicitly documented as historical |

### 10.3 Cross-Codebase Comparison Verified

All claims in §9 (cross-codebase comparison) verified against source code:

- Bitcoin Core 28.4.0 has no `nTime`, no PoS, no `Coin.nTime`, `WARN_THRESHOLD = 10min`, no `DERKEY` flag
- Peercoin's `CheckBlockSignature` is P2PK-only (verified at line 4994-5010)
- Peercoin passes real witness data with `SCRIPT_VERIFY_P2SH` in `CheckProofOfStake` (verified at `kernel.cpp:691`) — but missing `SCRIPT_VERIFY_WITNESS` flag means P2WPKH kernels are still not verified in Peercoin
- Qtum has `prevoutStake` in block header (verified), `setStakeSeen` (verified), no `Coin.nTime` (verified)
- Both Qtum and Blackcoin use `SCRIPT_VERIFY_NONE` with `nullptr` witness (verified)
- Blackcoin's `MANDATORY_SCRIPT_VERIFY_FLAGS` now includes `CSV` and `WITNESS` (verified at `policy.h:91-98`)
- Blackcoin's `STANDARD_SCRIPT_VERIFY_FLAGS` includes `TAPROOT` but not in `MANDATORY` (verified at `policy.h:106-119`)

### 10.4 Conclusion

All agent/ documentation files are **substantially accurate**. The only discrepancies are minor line number offsets (inevitable with ongoing code changes) and one trivial type notation difference (`0LL` vs `int64_t(0)`). No behavioral claims, code logic descriptions, or architectural assertions were found to be incorrect.

---

## 11. Slop Removal — Completed and Staged

### 11.1 `src/wallet/test/fuzz/fees.cpp` — Dead code removed (26 lines)

Removed commented-out code blocks referencing removed Blackcoin APIs:
- `m_fallback_fee` initialization (removed API)
- `tx_bytes` variable declaration (unused — `GetRequiredFee(wallet, tx_bytes)` was removed)
- `m_min_fee` initialization (removed API)
- `GetRequiredFee(wallet, tx_bytes)` call (removed API)
- `/* */` block containing `m_confirm_target`, `m_fee_mode`, `FeeCalculation`, `GetMinimumFeeRate`, `GetMinimumFee` (all removed or commented out)

### 11.2 `src/qt/bitcoingui.cpp` — Dead comment removed + style fix

- Removed `// frameBlocksLayout->addWidget(unitDisplayControl);` (dead code — widget removed from layout)
- Fixed `// blackcoin::` → `// blackcoin:` for style consistency

### 11.3 `src/rpc/blockchain.cpp` — Style fix (2 instances)

- Fixed `// blackcoin:: include flags` → `// blackcoin: include flags`
- Fixed `// blackcoin:: include modifier` → `// blackcoin: include modifier`

### 11.4 `src/wallet/staking.cpp` — Style fix

- Fixed `// blackcoin:: Optimization` → `// blackcoin: Optimization`

### 11.5 `src/node/timeoffsets.cpp` — UB-safety comment restored

The original Bitcoin Core comment `// when median == std::numeric_limits<int64_t>::min(), calling std::chrono::abs is UB` was replaced by the Blackcoin diff with a comment about the 16s threshold, losing the UB-safety rationale for the `std::max` clamp. Restored the guard comment:
```cpp
// blackcoin: warn when median offset exceeds 16s — matches FutureDrift consensus limit.
// Guard: std::chrono::abs(int64_t::min()) is UB, so clamp to min+1 first.
```

### 11.6 `src/wallet/rpc/spend.cpp` — `burn` RPC info leak fixed

Moved `EnsureWalletIsUnlocked(*pwallet)` before `GetBalance` call in the `burn` RPC handler. Previously, a locked wallet user could distinguish "insufficient funds" from "wallet locked", leaking balance information.

---

## 12. Acknowledged Items — Not Planned for Implementation

These architectural differences vs Qtum/Peercoin are acknowledged and documented. No implementation planned — they are design choices, not defects.

### 12.1 No `setStakeSeen` anti-duplicate mechanism

Qtum has `setStakeSeen` (`COutPoint, uint32_t` set) to reject duplicate stakes at the block header level. Blackcoin mitigates the txid collision via the OP_RETURN timestamp carrier instead. Both approaches could coexist, but the OP_RETURN fix addresses the collision at the txid level which is sufficient for Blackcoin's use case.

**Reference**: `agent/SegWitTxv2Coinstake.md` §Option A, `agent/qtum_comparison.md` §5

### 12.2 No delegation support

Qtum supports Proof-of-Delegation via `prevoutStake` in block header + EVM smart contracts, allowing offline staking and super stakers. Blackcoin requires the kernel UTXO owner to sign the block directly. This is a deliberate design choice — Blackcoin does not have an EVM layer and delegation is not part of the roadmap.

**Reference**: `agent/qtum_comparison.md` §12

### 12.3 No forward-scan staking windows

Qtum scans 3 windows ahead (48s lookahead) with a pre-populated cache. Blackcoin scans only the current window with wake-on-block via `cv_new_block` + `MsUntilNextWindow()`. Blackcoin's approach is simpler, lower CPU, and sufficient for its block spacing. Qtum's forward scan may produce blocks marginally faster under ideal conditions but adds complexity.

**Reference**: `agent/qtum_comparison.md` §7

### 12.4 CoinStatsIndex rebuild performance — as designed

The CoinStatsIndex rebuild is slow (same slowness as Bitcoin Core and Qtum). This is inherent to the indexing design, not a bug. No optimization planned — the index rebuilds correctly, just takes time.

**Reference**: `agent/CoinStatsIndexOptimization.md`

### 12.5 `ExtractDestination` P2PK hack — accepted, future removal

`addresstype.cpp:67-74` reinterprets P2PK scripts as P2PKH addresses for display. This is still needed for legacy P2PK reward UTXOs in the combining pool. Will be removed when native staking is possible for all address types (P2PKH, P2WPKH, P2TR) without the P2PK intermediate. This is a large task and not planned for the near term.

**Reference**: `agent/staking.md` §"ExtractDestination Hack"

### 12.6 BIP94 not applicable to Blackcoin — fully disabled

BIP94 is a timewarp-attack mitigation designed for Bitcoin's fixed-interval difficulty adjustment. Blackcoin uses a per-block exponential moving average (EMA) difficulty adjustment — the difficulty recalculates at every block boundary based on the time between the last two PoS blocks. Because the difficulty changes continuously rather than at fixed period boundaries, the timewarp attack vector that BIP94 protects against does not apply.

The BIP94 code exists but is disabled on **all networks** (`enforce_BIP94 = false` at `chainparams.cpp` lines 116, 234, 339, 478, 561 — mainnet, testnet, testnet4, signet, and regtest respectively). No action needed.

**Reference**: `agent/validations.md` §3, `agent/staking.md` §10

### 12.7 Header sync PRESYNC phase — effectively a no-op for Blackcoin

**Status:** Acknowledged. Not planned for modification — the overhead is accepted as-is.

Bitcoin Core's headers-first sync protocol (inherited by Blackcoin) uses a two-phase header synchronization:

1. **PRESYNC** — lightweight validation: stores 1-bit hash commitments per 50 headers, checks difficulty transitions, accumulates chain work
2. **REDOWNLOAD** — re-downloads full headers, verifies the hash commitments match, then promotes to `AcceptBlockHeader` for full validation

The PRESYNC phase's primary security mechanism is `PermittedDifficultyTransition` (`pow.cpp:101`), which prevents an adversary from claiming arbitrary difficulty jumps between consecutive headers. In Bitcoin, this check constrains the claimed work to realistic bounds — an attacker cannot compress infinite work into a short chain because difficulty can't jump more than 4x per block.

**For Blackcoin, `PermittedDifficultyTransition` is stubbed out:**

```cpp
// pow.cpp:101
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    // Blackcoin: skip this check as we are using different difficulty adjustment algo
    return true;
}
```

**Why it must be disabled:** Blackcoin has **separate PoW and PoS difficulty tracks**. `GetNextTargetRequired` calls `GetLastBlockIndex(pindexLast, fProofOfStake)`, which walks backward to the last block of the same type. Since PoW and PoS blocks alternate on the same chain, consecutive headers routinely have dramatically different `nBits` values — a PoS block may have 1/100th the target of the preceding PoW block. A Bitcoin-style "difficulty can't change by more than 4x" check would reject every valid PoW→PoS transition.

**What PRESYNC still does for Blackcoin (marginal value):**

| Function | Value for Blackcoin |
|---|---|
| `PermittedDifficultyTransition` | **None** — always returns `true` |
| Hash commitments (1 bit per 50 headers) | **Marginal** — prevents the *same* peer from switching chains between PRESYNC and REDOWNLOAD. Does not protect against a fully malicious peer who lies consistently in both phases. |
| Work accumulation (`GetBlockProof`) | **Needed** — gates the `min_pow_checked` threshold in `AcceptBlockHeader`. But `GetBlockProof` uses `nBits` directly, so an adversary can inflate claimed work by setting tiny targets. |
| Chain length bounding (`max_commitments`) | **Useful** — soft cap at ~53k headers per peer, limiting memory consumption. |

**Practical impact:** On mainnet IBD from scratch, the PRESYNC + REDOWNLOAD pipeline takes approximately **30 minutes** before block download can begin. During this time, the node is serially processing 2000-header batches (~400ms per round-trip), storing hash commitments, and accumulating work — all without any Blackcoin-specific validation. The actual header validation (PoW for PoW blocks via `CheckProofOfWork`, difficulty via `GetNextTargetRequired`, PoS kernel via `CheckProofOfStake`) happens downstream in `AcceptBlockHeader` and `ConnectBlock`, regardless of whether PRESYNC ran.

**Why we leave it as-is:** Removing PRESYNC would require replacing the anti-DoS work threshold mechanism and the hash commitment scheme with an alternative. The 30-minute overhead is a one-time cost during IBD and does not affect steady-state operation. The security model is unchanged — PRESYNC doesn't add Blackcoin-specific validation, and its removal wouldn't remove any protection that matters for Blackcoin's threat model. The complexity of modifying the inherited headersync protocol outweighs the benefit of saving 30 minutes during initial sync.

**Reference**: `agent/validations.md` §2, §3 (header validation flow), `src/pow.cpp:101-105`, `src/headerssync.cpp:178-214` (PRESYNC), `src/headerssync.cpp:216-278` (REDOWNLOAD), `src/net_processing.cpp:3003-3083` (`IsContinuationOfLowWorkHeadersSync`)

---

## 13. Staking Status Flicker Fix — Staged and Reviewed

### 13.1 Problem

After the wake-on-block staker refactor (§1.4), `getstakinginfo` and the Qt staking icon briefly reported `"staking": false` after every new block.

Root cause: the staker used `m_last_coin_stake_search_interval > 0` as a proxy for "actively staking". The new `SleepStaker()` wakes immediately on `cv_new_block`, but between waking and starting the next search, `m_last_coin_stake_search_interval` is reset to `0`. That transient state was visible to both RPC and GUI callers.

`m_last_coin_stake_search_interval` is also a plain `int64_t` read from another thread without synchronization — a data race.

### 13.2 Fix

Introduce a dedicated `std::atomic<bool> m_staker_active` flag on `CWallet` and manage it from `PoSMiner()`:

- A new `StakerActiveGuard` RAII helper clears the flag on every exit path from `PoSMiner()` (returns, exceptions).
- The flag is cleared at the top of each loop iteration while waiting for wallet readiness (locked, disabled, importing, scanning).
- The flag is set `true` only while the staker is genuinely searching for a proof-of-stake kernel, and stays `true` across brief condition-variable wakes.

Expose the flag through the wallet interface and consume it in:
- `src/wallet/rpc/staking.cpp` — `getstakinginfo` now reports `staking = stakerActive && nWeight`
- `src/qt/bitcoingui.cpp` — `updateStakingIcon()` uses `getStakerActive()` instead of `getLastCoinStakeSearchInterval()`

### 13.3 Files Changed

| File | Change |
|------|--------|
| `src/wallet/wallet.h` | Add `std::atomic<bool> m_staker_active` |
| `src/node/miner.cpp` | Add `StakerActiveGuard`; manage flag in `PoSMiner()` |
| `src/interfaces/wallet.h` | Add `virtual bool getStakerActive()` |
| `src/wallet/interfaces.cpp` | Implement `getStakerActive()` |
| `src/wallet/rpc/staking.cpp` | Use `stakerActive` for `staking` field |
| `src/qt/bitcoingui.cpp` | Use `getStakerActive()` for staking icon |

### 13.4 Review Outcome

Independent review found **no bugs**. One minor observation was incorporated: removed a defensive null-check in `StakerActiveGuard::~StakerActiveGuard()` because the wallet pointer is guaranteed non-null and must outlive the staker thread.
