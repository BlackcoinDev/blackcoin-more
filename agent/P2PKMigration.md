# Migrating Staking Outputs from P2PK to P2PKH / P2WPKH / P2TR

**Branch:** v28-SEGWIT
**Date:** 2026-06-26
**Status:** COMPLETED. Implemented and verified on regtest. `vout[1]` is always `OP_RETURN <pubkey> <timestamp>`, reward in `vout[2]` preserves the kernel's native script type. `bMinterKey` flag and P2PK intermediate output removed in dead-code cleanup (June 26). Combining scope corrected: all kernel types combine same-type UTXOs matching `scriptPubKeyKernel`; P2PKH kernels additionally sweep legacy P2PK rewards via `combineP2PK`. Combining guard also fixed (June 29): `pcoin.first->GetHash() != txNew.vin[0].prevout.hash` compared only the tx hash, rejecting all other outputs from the kernel's parent transaction; fixed to compare the full `COutPoint` so same-tx sibling UTXOs are not incorrectly filtered out.

---

## 0. Staking Compatibility Matrix

There are **two mechanisms** for block signing:

1. **P2PK in `vout[1]`** — the original mechanism. `SignBlock` reads the pubkey from the P2PK script, `CheckBlockSignature` verifies against it. Reward is in `vout[1]` (or `vout[1+bMinterKey]` for witness kernels).
2. **OP_RETURN carrier in `vout[1]`** (current) — `vout[1]` = `OP_RETURN <pubkey> [message]` (non-spendable), reward in `vout[2]`. `CheckBlockSignature` extracts the pubkey from the OP_RETURN and verifies the block signature against it (`validation.cpp:3895-3912`). This is **consensus-valid today** — no fork needed.

### 0.1 TxoutType Mapping (v28 Codebase)

In the v28 codebase, script types are mapped to integer values via the `TxoutType` enum (`src/script/solver.h`). When checking the `debug.log` for kernel types (`CreateCoinStake: added kernel type=N`), use this mapping:

- **`0`** = `NONSTANDARD`
- **`1`** = `ANCHOR` *(Ephemeral anchors)*
- **`2`** = `PUBKEY` *(P2PK)*
- **`3`** = `PUBKEYHASH` *(P2PKH)*
- **`4`** = `SCRIPTHASH` *(P2SH)*
- **`5`** = `MULTISIG`
- **`6`** = `NULL_DATA` *(OP_RETURN)*
- **`7`** = `WITNESS_V0_SCRIPTHASH` *(P2WSH)*
- **`8`** = `WITNESS_V0_KEYHASH` *(P2WPKH)*
- **`9`** = `WITNESS_V1_TAPROOT` *(P2TR)*
- **`10`** = `WITNESS_UNKNOWN`

### 0.2 Output Generation by Kernel Type

| Kernel type | `vout[1]` (carrier) | `vout[2]` (reward) |
|---|---|---|
| **PUBKEY (P2PK)** | `OP_RETURN <pubkey> <timestamp>` | **P2PKH** (upgraded) |
| **PUBKEYHASH (P2PKH)** | `OP_RETURN <pubkey> <timestamp>` | **P2PKH** |
| **WITNESS_V0_KEYHASH** | `OP_RETURN <pubkey> <timestamp>` | **P2WPKH** |
| **WITNESS_V1_TAPROOT** | `OP_RETURN <pubkey> <timestamp>` | **P2TR** |

Historically, the Blackcoin codebase *forced* all rewards to be converted into P2PK outputs regardless of what type of coin you staked. This was because the consensus code (`CheckBlockSignature`) required a raw public key in the output to verify the block signature.

We solved this by pushing the block-signing public key into a non-spendable `OP_RETURN` carrier in `vout[1]` (visible in JSON). This decoupled the block signature from the reward output, freeing `vout[2]` to inherit whatever script type the staked coin originally was.

**Why is there a `<timestamp>` in the `OP_RETURN` carrier?**
The 4-byte timestamp is pushed into the `OP_RETURN` carrier directly after the public key to prevent **TxID collisions**. In v2 transactions, `nTime` is completely stripped from serialization and the signature hash. If a node stakes a UTXO, the block gets orphaned, and the wallet retries staking the exact same UTXO, the resulting transaction bytes would be 100% identical (causing a TxID collision). By injecting the current PoS timestamp into the `OP_RETURN` carrier, the transaction outputs physically change every 16 seconds (the PoS window), guaranteeing a unique TxID for every staking attempt. `CheckBlockSignature` safely ignores this extra timestamp data.

### Current state — OP_RETURN carrier implemented (reward preserves kernel type, except P2PK which upgrades to P2PKH)

| Kernel | Wallet | Reward output | Kernel sig verified? | Block signing | Status |
|---|---|---|---|---|---|
| P2PK | Legacy | P2PKH (via carrier upgrade) | ✅ Yes | OP_RETURN carrier, `key.Sign()` | ✅ Works |
| P2PK | Descriptor | P2PKH (via carrier upgrade) | ✅ Yes | OP_RETURN carrier, `SignBlockHash()` | ✅ Works |
| P2PKH | Legacy | P2PKH (via carrier) | ✅ Yes | OP_RETURN carrier, `key.Sign()` | ✅ Works |
| P2PKH | Descriptor | P2PKH (via carrier) | ✅ Yes | OP_RETURN carrier, `SignBlockHash()` | ✅ Works |
| P2WPKH | Legacy | P2WPKH (via carrier) | ✅ Yes | OP_RETURN carrier, `SignBlockHash()` | ✅ Works |
| P2WPKH | Descriptor | P2WPKH (via carrier) | ✅ Yes | OP_RETURN carrier, `SignBlockHash()` | ✅ **Verified regtest** |
| P2TR | Descriptor | P2TR (via carrier) | ✅ Yes | OP_RETURN carrier, `SignBlockHash()` | ✅ **Verified regtest** |
| P2TR | Legacy | — | — | — | ❌ P2TR not mineable in legacy wallets |
| P2SH / P2WSH | Any | — | — | — | ❌ Rejected by CreateCoinStake (staking.cpp:328-332) |

### No-fork — OP_RETURN carrier with native reward type (current implementation)

`vout[1]` = `OP_RETURN <pubkey> <timestamp>`, reward in `vout[2]` as the kernel's native type. Block signed by ECDSA via the OP_RETURN carrier pubkey. No consensus change. This is **proven on-chain** — a real staked block (`txid acd7b37c...`, ~427k confirmations) uses `OP_RETURN <pubkey> "STAND FOR PEACE!"` under current consensus rules.

| Kernel | Wallet | Reward output | Block signing |
|---|---|---|---|
| P2PK | Any | P2PK/P2PKH/P2WPKH/P2TR | OP_RETURN carrier, ECDSA |
| P2PKH | Any | P2PK/P2PKH/P2WPKH/P2TR | OP_RETURN carrier, ECDSA |
| P2WPKH | Any | P2PK/P2PKH/P2WPKH/P2TR | OP_RETURN carrier, ECDSA |
| P2TR | Descriptor | P2PK/P2PKH/P2WPKH/P2TR | OP_RETURN carrier, ECDSA |
| P2TR | Legacy | — | ❌ P2TR not mineable in legacy wallets |

### Hard fork — native P2PKH/P2WPKH/P2TR in `vout[1]` (no carrier)

Currently, the consensus rule `CheckBlockSignature` (`validation.cpp:3876-3912`) requires a raw public key to verify the block's ECDSA signature. It only knows how to extract this raw public key from two script types in `vout[1]`:
1. **P2PK** (`<pubkey> OP_CHECKSIG`)
2. **OP_RETURN Carrier** (`OP_RETURN <pubkey>`)

If we wanted to put a native P2PKH or P2WPKH reward directly into `vout[1]` without using an `OP_RETURN` carrier, we would hit a massive cryptographic roadblock: **P2PKH and P2WPKH scripts only contain the *hash* of the public key, not the public key itself.** 
Without the raw public key, `CheckBlockSignature` cannot verify the block signature.

To fix this natively, we would have to rewrite `CheckBlockSignature` to pull the raw public key from somewhere else (like the `scriptSig` or block header). However, if new nodes started generating and accepting blocks using this new logic, **older nodes would instantly reject them** because their old `CheckBlockSignature` code would fail to find a raw public key in `vout[1]`. This would result in a permanent chain split (a Hard Fork). 

**Not recommended** — The `OP_RETURN` carrier approach elegantly bypasses this entire problem without changing consensus rules.

### Legend

- **Legacy wallet** (`BerkeleyDatabase`, BDB): supports P2PK, P2PKH, P2SH, P2SH-P2WPKH, P2WPKH (bech32). Does **not** support P2TR — `GetReservedDestination` rejects `BECH32M` (scriptpubkeyman.cpp:309), `IsMineInner` returns NO for `WITNESS_V1_TAPROOT` (scriptpubkeyman.cpp:117).
- **Descriptor wallet** (`SQLite`): supports all output types including P2TR.
- **Kernel sig verified?**: whether `CheckProofOfStake` checks the coinstake input signature. All kernels are securely verified via `VerifyScript` using `SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT` (the "Peercoin Fix" is implemented).
- **OP_RETURN carrier**: `vout[1]` = `OP_RETURN <33-byte-pubkey> [optional message]`. `CheckBlockSignature` reads 2 `GetOp`s (OP_RETURN + pubkey), ignores trailing bytes. Block signature is ECDSA verified against the recovered pubkey. **This is consensus-valid today** (`validation.cpp:3895-3912`) — no fork needed.
- **P2SH / P2WSH**: never accepted as kernels (staking.cpp:328-332).
- **Taproot activation status** (as of June 2026):

  | Network | Status |
  |---|---|
  | **Mainnet** | BIP9 signaling to start with next release (late 2026), not yet active |
  | **Testnet** | `locked_in` since block 2,850,000 → activates at block 2,865,000 |
  | **Regtest** | `ALWAYS_ACTIVE` |

  The soft fork (§10) does **not** depend on Taproot activation — it fixes P2WPKH kernel signature verification (the only witness type on-chain today). The two deployments are independent.

- **P2TR signing fix** (June 2026): `SCRIPT_VERIFY_TAPROOT` was missing from `STANDARD_SCRIPT_VERIFY_FLAGS` in `policy.h`. Without it, the signing test verification at `sign.cpp:572` treated P2TR as a no-op (`VerifyWitnessProgram` returns `set_success` without checking the witness when `SCRIPT_VERIFY_TAPROOT` is not in flags). This caused `::SignTransaction` to return `true` on the first (wrong) `ScriptPubKeyMan` instead of falling through to the correct P2TR descriptor. Fix: added `SCRIPT_VERIFY_TAPROOT` to `STANDARD_SCRIPT_VERIFY_FLAGS` (standard-only, not mandatory — peers not banned for invalid P2TR on networks where Taproot is not yet consensus). Also added `SCRIPT_VERIFY_WITNESS` and `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` to `MANDATORY_SCRIPT_VERIFY_FLAGS`. See `agent/P2TRSigningFix.md`.

---

## 1. The Current Design: OP_RETURN Carrier (Implemented June 2026)

The OP_RETURN carrier fix is **implemented and verified on regtest**. The staking reward output in the coinstake transaction now preserves the kernel's **native script type**, carried via an OP_RETURN block-signing carrier in `vout[1]`:

| Kernel type | `vout[1]` (carrier) | `vout[2]` (reward) |
|---|---|---|
| PUBKEY | `OP_RETURN <pubkey> <timestamp>` | P2PK (kernel script) |
| PUBKEYHASH | `OP_RETURN <pubkey> <timestamp>` | P2PKH |
| WITNESS_V0_KEYHASH | `OP_RETURN <pubkey> <timestamp>` | P2WPKH (kernel script) |
| WITNESS_V1_TAPROOT | `OP_RETURN <pubkey> <timestamp>` | P2TR (kernel script) |

`SignBlock` (`miner.cpp:650-692`) extracts the pubkey from either P2PK or OP_RETURN carrier. `CheckBlockSignature` (`validation.cpp:3879-3924`) already accepted the OP_RETURN carrier path — no consensus change was needed.

### Why P2PK was the bottleneck

1. **`SignBlock`** (`miner.cpp:650-692`) reads `block.vtx[1]->vout[1]` and requires `Solver(...) == TxoutType::PUBKEY`. It extracts the pubkey directly from the script and signs the block hash with the corresponding private key.
2. **`CheckBlockSignature`** (`validation.cpp:3879-3924`) does the same for verification. The only non-PUBKEY path is the `OP_RETURN` fallback (lines 3895-3912), which extracts a pubkey from an `OP_RETURN <pubkey>` carrier.

So the block-signing key is **carried in plaintext** in `vout[1]` — either as a bare P2PK script or as an OP_RETURN payload. The signature scheme is ECDSA over `block.GetHash()`.

### What must be preserved

Whatever output type we emit, the block must still be **signable** (the staker has the private key) and **verifiable** (any node can check the block signature without wallet/keystore access — `CheckBlockSignature` is a `static` function with no wallet access).

---

## 2. The Three Target Types

### 2.1 P2PKH — `OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG`

**The problem:** `CheckBlockSignature` cannot look up a pubkey from a hash. It is a static function with no keystore access.

**The solution already exists: ECDSA pubkey recovery.** The bundled libsecp256k1 includes the recovery module (`src/secp256k1/src/modules/recovery/`), with two wrappers already available:
- **`CKey::SignCompact(hash, vchSig)`** (`key.cpp:249-270`): 65-byte recoverable signature. Used today by `signmessagewithprivkey` RPC.
- **`CPubKey::RecoverCompact(hash, vchSig)`** (`pubkey.cpp:296-314`): recovers a `CPubKey` from a 65-byte compact signature. Used today by `verifymessage`.

**How P2PKH block signing would work:**

| Step | `SignBlock` | `CheckBlockSignature` |
|---|---|---|
| 1 | Solve `vout[1]` as PUBKEYHASH, extract `hash160` | Same |
| 2 | Look up private key by `CKeyID(hash160)` | — |
| 3 | `key.SignCompact(block.GetHash(), vchBlockSig)` | — |
| 4 | — | `CPubKey::RecoverCompact(block.GetHash(), vchBlockSig)` |
| 5 | — | `Hash160(recovered.GetID()) == hash160?` |

**No keystore needed on the verifier side.** The same mechanism Bitcoin uses for `signmessage`/`verifymessage`. Signature size: 65 bytes (vs 73-72 for DER-encoded ECDSA).

### 2.2 P2WPKH — `OP_0 <20-byte-hash>`

**Two options:**
- **Option A (compact-sig + recovery):** The block-signing key is still ECDSA. `SignBlock` looks up the key by `hash160`, signs compact, and `CheckBlockSignature` recovers the pubkey. Same mechanism as P2PKH.
- **Option B (OP_RETURN carrier, existing):** `vout[1] = OP_RETURN <pubkey>`, reward in `vout[2]` as P2WPKH. Already implemented.

### 2.3 P2TR — `OP_1 <32-byte-key>`

Taproot uses **BIP340 Schnorr**, not ECDSA. This breaks the P2PKH recovery approach (Schnorr signatures are not recoverable).

**Three options:**
- **Option A (OP_RETURN carrier + Schnorr verify):** Extend `CheckBlockSignature` to detect key type from pubkey length (33 bytes → ECDSA, 32 bytes → Schnorr). `SignBlock` uses `CKey::SignSchnorr` (`key.cpp:272`).
- **Option B (compact-sig with tweaked key):** Not viable — Taproot output keys are Schnorr, not ECDSA.
- **Option C (33-byte ECDSA pubkey in OP_RETURN):** Defeats the purpose of Taproot.

**Recommendation:** Option A for now. Current implementation uses ECDSA via the OP_RETURN carrier (the block is signed by the carrier pubkey, not by the P2TR key).

---

## 3. The Fork Question

`CheckBlockSignature` is consensus-critical:

| Target type | Fork type | Why |
|---|---|---|
| **P2PKH in `vout[1]`** | Hard fork | Old nodes reject `Solver != PUBKEY` and `Solver != NULL_DATA` |
| **P2WPKH in `vout[1]`** | Hard fork | Same — old nodes don't accept non-PUBKEY, non-OP_RETURN |
| **P2TR in `vout[1]`** | Hard fork | Same |

**All three require a hard fork** unless we keep `vout[1]` as OP_RETURN carrier (the current approach) and put the new output type in `vout[2+]`. That is the current design — **no fork needed**.

---

## 4. Solving the v2 TxID Collision via OP_RETURN Carrier (No Fork)

### 4.1 The problem

v2 coinstakes strip `nTime` from serialization and sighash (`transaction.h:232-236`, `interpreter.cpp:1333-1335, 1609-1612`). RFC6979 deterministic signatures produce identical `scriptSig` across retries with the same UTXO → same non-witness bytes → **same txid** on orphan+retry. See `agent/SegWitTxv2Coinstake.md`.

### 4.2 The fix: embed the in-memory timestamp as a 3rd OP_RETURN push

The OP_RETURN carrier in `vout[1]` is part of the serialized transaction and therefore part of the txid. `CheckBlockSignature` (validation.cpp:3895-3912) reads exactly **2** `GetOp` calls — `OP_RETURN` + `pubkey` — and ignores everything after. So a 3rd push is:
- ✅ **Included in the txid** (serialized in `vout[1]`)
- ✅ **Ignored by consensus** (verifier only reads 2 GetOps)
- ✅ **Free to vary** per 16-second window

```
vout[1]: OP_RETURN <33-byte-pubkey> <4-byte-LE-timestamp>
                                ^^^^^^^^^^^^^^^^^^^^^^^^
                                txNew.nTime (masked to 16s boundary)
                                varies per window → txid varies per window
```

The timestamp is `txNew.nTime`, already available in memory (masked: `nTime & ~0xf`). Each retry after an orphan lands in a **different 16-second window** → different timestamp → different `vout[1]` → **different txid**. The block signature is unchanged (sighash excludes `nTime` for v2), but the txid now varies because the output script varies.

### 4.3 Why this is safe

1. **Consensus**: `CheckBlockSignature` reads only 2 `GetOp`s. The 3rd push is never parsed by any consensus code.
2. **Proven on-chain**: a staked block (txid `acd7b37c...`, ~427k confirmations) uses `OP_RETURN <pubkey> "STAND FOR PEACE!"` — a 3rd push accepted by consensus today.
3. **No fork**: uses only existing consensus rules.

### 4.4 Cost

- 5 bytes per coinstake (1 byte push opcode `0x04` + 4 bytes LE uint32 timestamp)
- A non-spendable OP_RETURN output per staked block (~34 bytes)

### 4.5 Comparison with other fixes

| Fix | Breaks txid collision? | Fork? | Effort |
|---|---|---|---|
| `setStakeSeen` (Option A) | ❌ No — prevents *acceptance*, not the collision | No | ~20 lines |
| `prevoutStake` in header (Option B) | ✅ Yes — block hash changes too | Soft fork | Large |
| OP_RETURN carrier + timestamp (Option E) | ✅ Yes — txid varies per window | **No** | ~10 lines |

---

## 5. Implementation Details — Combining Guard (What Shipped)

With `bMinterKey` removed and combining enabled for all kernel types, the shipped guard in `CreateCoinStake` (`staking.cpp:395-435`) is:

```cpp
// Build the legacy P2PK script from the carrier pubkey so old P2PK reward
// UTXOs can still be swept into coinstakes via input combining.
CScript scriptPubKeyP2PK = CScript() << ToByteVector(minterPubKey) << OP_CHECKSIG;
bool combineP2PK = (whichType == TxoutType::PUBKEYHASH);

for (const std::pair<const CWalletTx*, unsigned int> &pcoin : setCoins)
{
    // Same-type combining for all kernel types;
    // P2PKH kernels additionally sweep legacy P2PK rewards.
    if (txNew.vout.size() == 3 && (pcoin.first->tx->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel
        || (combineP2PK && pcoin.first->tx->vout[pcoin.second].scriptPubKey == scriptPubKeyP2PK))
        && COutPoint(pcoin.first->GetHash(), pcoin.second) != txNew.vin[0].prevout)
    {
        // ...
        // Use nTimeSmart (block time), not tx->nTime (always 0 for v2)
        if (pcoin.first->nTimeSmart > txNew.nTime)
            continue;
    }
}
```

Key design points:
1. **No `whichType` guard** — all four supported kernel types get same-type input combining via `scriptPubKeyKernel` match.
2. **`combineP2PK` gates the P2PK sweep** — only P2PKH kernels (the type that historically produced P2PK rewards) additionally match `scriptPubKeyP2PK`. P2WPKH/P2TR kernels skip the P2PK sweep.
3. **`nTimeSmart` replaces `tx->nTime`** — `pcoin.first->tx->nTime` is always 0 for v2 txs (nTime not serialized for version >= 2, transaction.h:232-236). The guard was a no-op. Fixed by using `pcoin.first->nTimeSmart` (block time for confirmed txs). Note: `Coin.nTime` is also 0 for v2 (since `AddCoins` stores `tx.nTime` directly), but `nTimeSmart` is always the real block time regardless of tx version.
4. **`COutPoint` comparison fix (June 29)** — `pcoin.first->GetHash() != txNew.vin[0].prevout.hash` compared only the tx hash, rejecting all other outputs from the kernel's parent transaction. For example, an `optimizeutxoset` creating 42 identical P2WPKH outputs would only combine one of them with the kernel (and only if it was from a different tx). Fixed to compare the full `COutPoint(pcoin.first->GetHash(), pcoin.second) != txNew.vin[0].prevout`, allowing same-tx sibling UTXOs to be correctly combined.

### Amount-setting indices (post-fix)

Reward is always at `vout[2]`:

| Case | vouts | size check | reward at | split at | devfund at |
|---|---|---|---|---|---|
| No split, no dev | marker+carrier+reward = 3 | else | `vout[2]` | — | — |
| Split, no dev | marker+carrier+reward+split = 4 | if | `vout[2]` | `vout[3]` | — |
| Split, dev | marker+carrier+reward+split+dev = 5 | if | `vout[2]` | `vout[3]` | `vout[4]` |
| No split, dev | marker+carrier+reward+dev = 4 | else | `vout[2]` | — | `vout[3]` |

---

## 6. On-Chain Audit & The "Peercoin Fix"

**Date:** June 2026
**Status:** Implemented in v28.4.0

### Background

In older versions of Blackcoin, `CheckProofOfStake` called `VerifySignature` which passed `nullptr` for the witness and used `SCRIPT_VERIFY_NONE`. This meant that P2WPKH and P2TR kernels had **no actual signature verification** — their security relied entirely on the kernel hash.

Peercoin fixed this vulnerability by calling `VerifyScript` directly with the actual witness data and `SCRIPT_VERIFY_P2SH`.

### The Fix

Blackmore284 has completely implemented this fix. In `pos.cpp:174-175`:

```cpp
TransactionSignatureChecker checker(&tx, 0, coinPrev.out.nValue, txdata, MissingDataBehavior::ASSERT_FAIL);
if (!VerifyScript(txin.scriptSig, coinPrev.out.scriptPubKey, &txin.scriptWitness,
    SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT, checker)) {
    return state.Invalid(BlockValidationResult::BLOCK_INVALID_HEADER, "stake-verify-signature-failed" ...);
}
```

This ensures full cryptographic security for all modern address types.

### On-Chain Audit

Prior to enforcing this soft fork, we scanned all coinstake transactions from SegWit activation to the chain tip on both testnet and mainnet to ensure no existing blocks would be rejected.

| Network | SegWit activation | Scanned to | Total coinstakes | Witness kernels | P2WPKH | P2TR | Malformed |
|---|---|---|---|---|---|---|---|
| **Testnet** | 2,070,000 | 2,852,570 | 782,417 | 61,196 | 61,196 | 0 | 0 |
| **Mainnet** | 5,805,000 | 5,928,105 | 123,106 | 3 | 3 | 0 | 0 |

All witness kernels were P2WPKH with well-formed `[DER-sig, compressed-pubkey]` witness stacks. The soft fork was safely deployed.

---

## Files Referenced

### Block signing / verification
- `src/node/miner.cpp:650-692` — `SignBlock()` (reads `vout[1]`, requires PUBKEY or NULL_DATA)
- `src/validation.cpp:3879-3924` — `CheckBlockSignature()` (reads `vout[1]`, verifies PUBKEY or OP_RETURN carrier)

### Coinstake construction
- `src/wallet/staking.cpp:252-524` — `CreateCoinStake()` (builds outputs)
- `src/wallet/staking.cpp:328-387` — kernel type handling
- `src/wallet/staking.cpp:395-435` — input combining (`combineP2PK`, `nTimeSmart`, `scriptPubKeyP2PK`)

### Signature primitives
- `src/key.cpp:249-270` — `CKey::SignCompact()` (65-byte recoverable ECDSA)
- `src/pubkey.cpp:296-314` — `CPubKey::RecoverCompact()` (recover pubkey from 65-byte sig)
- `src/key.cpp:272-` — `CKey::SignSchnorr()` (BIP340 Schnorr)

### Kernel verification
- `src/pos.cpp:130-180` — `CheckProofOfStake()` (uses `VerifyScript` with full flags)

### v2 txid collision fix
- `src/primitives/transaction.h:232-236, 274-277` — nTime stripped for v2
- `src/script/interpreter.cpp:1333-1335, 1609-1612` — both sighash paths strip nTime for v2
- `src/wallet/staking.cpp` — carrier mechanism in `CreateCoinStake` (formerly `bMinterKey` block, now unified)
- `agent/SegWitTxv2Coinstake.md` — full collision analysis

### Peercoin
- `../peercoin/src/kernel.cpp:686-692` — CheckProofOfStake with real witness + SCRIPT_VERIFY_P2SH
- `../peercoin/src/validation.cpp:4994-5010` — CheckBlockSignature (PUBKEY only)

---

## 7. SignKey vs Peercoin mintkey — Staking Address Comparison

### Blackcoin (SignKey)

Blackcoin uses a dedicated `AddressPurpose::SIGNKEY` enum value (`src/wallet/types.h:65`) to mark staking addresses:

```cpp
enum class AddressPurpose {
    RECEIVE,
    SEND,
    REFUND,
    SIGNKEY, //!< Used by PoS staker to hold a private key for signing blocks
};
```

The staker (`miner.cpp:702-724`) looks up the address book for label `"SignKey"` with `AddressPurpose::SIGNKEY`:
```cpp
pwallet->ForEachAddrBookEntry([&](dest, label, is_change, purpose) {
    if (label == "SignKey" && purpose == AddressPurpose::SIGNKEY)
        dest = _dest;
});
```

If not found, creates a new P2PKH address with `GetNewDestination(OutputType::LEGACY, label)` and sets `AddressPurpose::SIGNKEY`.

### Peercoin (mintkey)

Peercoin uses a simple string label `"mintkey"` with no dedicated `AddressPurpose` enum value (Peercoin's `AddressPurpose` only has RECEIVE/SEND/REFUND):

```cpp
// peercoin/src/node/miner.cpp:568-577
const std::string label = "mintkey";
pwallet->ForEachAddrBookEntry([&](dest, label, is_change, purpose) {
    if (_is_change) return;
    if (_label == label)
        dest = _dest;
});

if (std::get_if<CNoDestination>(&dest)) {
    auto op_dest = pwallet->GetNewDestination(OutputType::LEGACY, label);
    dest = *op_dest;
}
```

### Comparison

| Feature | Blackcoin (SignKey) | Peercoin (mintkey) |
|---|---|---|
| Label name | `"SignKey"` (capitalized) | `"mintkey"` (lowercase, hyphenated) |
| `AddressPurpose` enum | `SIGNKEY` (dedicated value) | None (uses RECEIVE by default) |
| Filtering | Label + purpose enum | Label only |
| Output type | Legacy P2PKH | Legacy P2PKH |
| Supports P2TR | Yes (reward output) | No (legacy only) |

### Why it matters

The SignKey/mintkey address holds the private key used to **sign block headers** (via `SignBlock`). The pubkey is extracted from this address and placed in the OP_RETURN carrier (`vout[1]`). This is separate from the kernel's UTXO — the kernel can be any supported type (P2PK, P2PKH, P2WPKH, P2TR), but the block is always signed with the SignKey/mintkey's ECDSA key.

### `optimizeutxoset` and combining interaction

The `optimizeutxoset` RPC (`src/wallet/rpc/spend.cpp:351`) creates uniform UTXO outputs for staking. All outputs go to the same address with the same script type. Before the June 29 `COutPoint` fix, the combining loop in `CreateCoinStake` would only combine 1 output from each `optimizeutxoset` transaction with the kernel — all siblings from the same tx were rejected by the overly broad `GetHash() != prevout.hash` check. After the fix, all sibling UTXOs from the same transaction can be combined (up to the 250 BLK threshold and 10-input limit).
