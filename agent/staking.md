# Staking Flow Analysis

## Overview

Blackmore284 implements PoS (Proof of Stake) staking through several interconnected components. This document traces how legacy wallet staking works end-to-end.

## Files Involved

- `src/node/miner.cpp` — PoSMiner, SignBlock, CreateNewBlock
- `src/wallet/staking.cpp` — CreateCoinStake, SelectCoinsForStaking
- `src/validation.cpp` — CheckBlockSignature
- `src/pos.cpp` — CheckProofOfStake

## Staking Flow

### 1. Destination Resolution (PoSMiner, miner.cpp:747-768)

The minter key / staking reward address is resolved as follows:

1. Look up label `"SignKey"` with `AddressPurpose::SIGNKEY` from address book via `ForEachAddrBookEntry`
2. If not found, call `GetNewDestination(OutputType::LEGACY, label)` to create a new P2PKH address and set `AddressPurpose::SIGNKEY`
3. The `dest` variable is a `CTxDestination` (P2PKH address)

**Important**: For legacy wallets, this `dest` is only used to create the block template via `CreateNewBlock(GetScriptForDestination(dest), ...)`. The actual reward destination in the coinstake transaction is determined by the kernel type, not this `dest`.

### 2. Block Assembly (CreateNewBlock, miner.cpp:141)

- Sets PoS difficulty via `GetNextTargetRequired(..., true)`
- Calls `wallet::CreateCoinStake(*pwallet, nBits, 1, txCoinStake, nFees, destination)` at line 265
- If coinstake found, inserts it as `vtx[1]` and sets coinbase to empty

### 3. Coin Selection and Kernel Finding (CreateCoinStake, staking.cpp:251-401)

1. Select UTXOs meeting depth/amount criteria via `SelectCoinsForStaking`
2. For each UTXO, scan backward in time to find a kernel (age * value ≥ target)
3. When kernel found, proceed to output construction

### 4. Kernel Type Handling (staking.cpp:318-377)

Four kernel types are supported:

#### PUBKEY (legacy)
- `scriptPubKeyOut = scriptPubKeyKernel` (P2PK output)
- Simple case: reward goes to same address as kernel

#### PUBKEYHASH (legacy)
- Gets private key from `GetLegacyScriptPubKeyMan()->GetKey(CKeyID(...), key)`
- Constructs `scriptPubKeyOut = <pubkey> OP_CHECKSIG` (P2PK output)
- Converts P2PKH kernel to P2PK output

#### WITNESS_V0_KEYHASH / WITNESS_V1_TAPROOT (descriptor)
- Gets signing provider from `destination` parameter (the `"SignKey"` address)
- Constructs `scriptPubKeyOut = <pubkey> OP_CHECKSIG` from destination's pubkey
- Sets `bMinterKey = true` — adds extra vout, redirects kernel output

### 5. Output Layout

#### Legacy (no bMinterKey)
```
vout[0]: empty (marker)
vout[1]: <pubkey> OP_CHECKSIG  ← reward (P2PK, from kernel)
vout[2]: <pubkey> OP_CHECKSIG  ← split reward (if nCredit ≥ 1000 COIN)
vout[3]: devfund (if enabled)
```

#### Descriptor with WITNESS kernel (bMinterKey=true)
```
vout[0]: empty (marker)
vout[1]: <pubkey> OP_CHECKSIG  ← reward from destination (P2PK)
vout[2]: WITNESS kernel       ← original kernel output redirected here (P2WPKH or P2TR)
vout[3]: WITNESS split (if ≥ 1000)
vout[4]: devfund (if enabled)
```

### 6. Signing (staking.cpp:476-500)

- **Legacy**: Uses `SignSignature(*GetLegacyScriptPubKeyMan(), ...)` to sign each coinstake input
- **Descriptor**: Uses `wallet.SignTransaction()` which signs via descriptor providers

### 7. Block Signing (SignBlock, miner.cpp:702-732)

1. Reads vout[1] (or vout[0] for PoW) from the coinstake
2. Checks `Solver(scriptPubKey, vSolutions) == TxoutType::PUBKEY`
3. Both legacy and descriptor produce P2PK in vout[1], so both paths work
4. **Legacy path**: Gets key from `GetLegacyScriptPubKeyMan()` and calls `key.Sign()`
5. **Descriptor path**: Calls `keystore.SignBlockHash()` with PKHash derived from pubkey

### 8. Validation (CheckBlockSignature, validation.cpp)

For PoS blocks:
- Checks vout[1] scriptPubKey type
- **PUBKEY**: Verifies block signature against the pubkey in the script
- **NULL_DATA (OP_RETURN)**: Extracts pubkey from OP_RETURN and verifies signature
- Other types: Rejected (hardened type guard)

### 9. Proof of Stake Verification (CheckProofOfStake, pos.cpp)

- Verifies the kernel UTXO meets age/value requirements
- Calls `VerifySignature(coinPrev, txin.prevout.hash, tx, 0, SCRIPT_VERIFY_NONE)` at line 157
- Ensures coinstake input is properly signed

#### Why SCRIPT_VERIFY_NONE

`VerifySignature` (sign.cpp:718) passes `nullptr` for the witness parameter:

```cpp
return VerifyScript(txin.scriptSig, txout.scriptPubKey, nullptr, flags, checker);
```

Inside `VerifyScript` (interpreter.cpp:1973):
- Line 2006: `if (flags & SCRIPT_VERIFY_WITNESS)` — if **not** set, witness programs are **completely skipped**
- Line 2023: `if (flags & SCRIPT_VERIFY_P2SH)` — if **not** set, P2SH redeem script execution is **skipped**

| Kernel type | scriptPubKey | With SCRIPT_VERIFY_NONE | With P2SH\|WITNESS |
|---|---|---|---|
| **P2PK** | `<pubkey> OP_CHECKSIG` | CHECKSIG verifies signature (proper) | CHECKSIG verifies signature (same) |
| **P2PKH** | `OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG` | CHECKSIG verifies signature (proper) | CHECKSIG verifies signature (same) |
| **P2WPKH** | `OP_0 <hash160>` | Executed as plain script: push 0, push hash — stack top truthy → **passes, no sig check** | Witness program detected → `VerifyWitnessProgram` with empty witness → expects 2 items → **fails** |
| **P2SH** | `OP_HASH160 <hash> OP_EQUAL` | Just checks hash match — **passes, no redeem script** | Requires valid redeem script in scriptSig → coinstake doesn't have one → **fails** |
| **P2TR** | `OP_1 <32-byte-key>` | Executed as plain script: push 1, push key — stack top truthy → **passes, no sig check** | Witness v1 program → empty witness → **fails** |

**Security implication**: P2WPKH / P2SH / P2TR kernels have **no actual signature verification** with `SCRIPT_VERIFY_NONE`. The scriptPubKey is executed as a plain script and always succeeds. Security for these kernel types relies entirely on the kernel hash computation. Only P2PK and P2PKH kernels are properly verified through CHECKSIG.

**Why not fix `VerifySignature` to pass witness data?** The root cause is two-fold: (1) `VerifySignature` passes `nullptr` witness, and (2) coinstake scriptSig doesn't carry a valid P2SH redeem script. Fixing the witness would fix P2WPKH/P2TR but not P2SH. This is longstanding behavior dating back to at least Blackcoin v1.2.5.2.

### 10. Why No TxIndex is Needed for Staking

Despite Blackcoin More enabling txindex by **default** (`DEFAULT_TXINDEX = true` in `txindex.h:10`, unlike upstream Bitcoin Core), the staking hot path never touches it. The kernel hash requires no historical transaction data — only three sources, all available without txindex:

| Kernel hash parameter | Source |
|---|---|
| `nStakeModifier` | `CBlockIndex::nStakeModifier` (chain tip block index, in-memory) |
| `txPrev.nTime` | `Coin::nTime` (UTXO set LevelDB, or `blockFrom->nTime` fallback via `GetAncestor`) |
| `prevout.hash` + `prevout.n` | Already known (the UTXO being checked) |
| `nWeight (amount)` | `Coin::out.nValue` (UTXO set LevelDB) |
| `nTimeTx` | Current block timestamp |

Three data sources make this work, even for UTXOs from 2014:

#### Source 1: Wallet DB (`mapWallet`)

On startup, `WalletBatch::LoadWallet` in `walletdb.cpp:1160` loads **every** transaction the wallet has ever owned into `CWalletTx::mapWallet` — with **no age filter**. `LoadTxRecords` (line 1028) iterates all TX-keyed records via cursor, and `CWallet::LoadToWallet` (wallet.cpp:1203) inserts each into `mapWallet`. Each `CWalletTx` carries a full `CTransactionRef tx`, so `tx->vout[n].nValue` and `tx->vout[n].scriptPubKey` are always available in memory.

`CreateCoinStake` at `staking.cpp:109` iterates `mapWallet` directly. An explicit comment at line 305-307 documents the optimization:

> *"We use the cached transaction data in CWalletTx instead of hitting disk with g_txindex->FindTx. This reduces block creation time from ~100s to <100ms."*

#### Source 2: UTXO Set LevelDB (`chainstate/`)

The `Coin` struct (`coins.h:34-93`) stores everything needed for the kernel hash:
- `CTxOut out` → `nValue`, `scriptPubKey` (transaction.h:149-153)
- `uint32_t nHeight : 31` (line 47)
- `unsigned int nTime` (line 50)

Serialization writes all fields to LevelDB (`coins.h:75-82`). `CCoinsViewDB::GetCoin` in `txdb.cpp:68-70` reads them back with a single LevelDB read by outpoint key. The staking code uses this at `pos.cpp:142, 160, 178, 198, 226, 239`:

```cpp
view.GetCoin(txin.prevout, coinPrev)          // line 142
// ... then uses:
coinPrev.nTime                                 // line 160
coinPrev.out.nValue                            // line 160
coinPrev.nHeight                               // line 147
(coinPrev.nTime ? coinPrev.nTime : blockFrom->nTime)  // line 160 fallback
```

#### Source 3: In-Memory Block Index (`chain.dat` / `CBlockIndex`)

`GetAncestor` in `chain.cpp:93-118` walks in-memory `pprev` and `pskip` pointers only — **never reads block files or txindex**. The `nTime` field is a plain `uint32_t` member of `CBlockIndex` (`chain.h:190`). Staking uses it as fallback at `pos.cpp:160, 198, 239`:

```cpp
CBlockIndex* blockFrom = pindexPrev->GetAncestor(coinPrev.nHeight);
// coinPrev.nTime ?: blockFrom->nTime
```

This works for any height, including UTXOs from block 1, because the entire block tree index is held in memory (in practice ~5GB for 800K+ blocks with all fields).

#### What TxIndex Is Actually Used For

TxIndex (`index/txindex.cpp`, `node/transaction.cpp:135`) is only needed for:
- **RPCs**: `getrawtransaction` on old/spent transactions
- **REST API**: Transaction lookups by txid
- **Backwards wallet lookup**: When a txid is requested that's no longer in `mapWallet`

None of these are on the staking hot path.

## Key Observations

1. **Destination only matters for descriptor wallets with WITNESS kernels** — legacy wallets ignore the `destination` parameter entirely in CreateCoinStake

2. **Both legacy and descriptor wallets produce P2PK in vout[1]** — this is critical for SignBlock and CheckBlockSignature compatibility

3. **The `"SignKey"` address book entry** (with `AddressPurpose::SIGNKEY`) is used by descriptor wallets to determine where rewards go when the kernel is a WITNESS type

4. **bMinterKey flag** controls whether an extra vout is added for the reward and the kernel output is redirected — only set for WITNESS_V0_KEYHASH and WITNESS_V1_TAPROOT kernels

5. **`SCRIPT_VERIFY_NONE`** is used because `VerifySignature` passes `nullptr` witness, and coinstake scriptSig lacks a P2SH redeem script. Adding `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS` would break verification for P2WPKH/P2SH/P2TR kernels (see [§9](#9-proof-of-stake-verification-checkproofofstake-poscpp) for details).

6. **`updatedBlockTip()` is now a pure wake signal** — it only sets `m_new_block_arrived = true` and notifies `cv_new_block`. All boundary/MTP/sleep math has moved to `miner.cpp`.

## 11. Staker Timing (PoSMiner Loop)

### 11.1 Single-responsibility design

After the refactor, staking timing lives entirely in `src/node/miner.cpp`:

1. **Wake signal** from `CWallet::updatedBlockTip()` (`wallet/wallet.cpp`):
   ```cpp
   m_new_block_arrived.store(true);
   cv_new_block.notify_one();
   ```
   The wallet no longer pre-calculates sleeps.

2. **Timer guard** inside `BlockAssembler::CreateNewBlock` (`miner.cpp:258`):
   ```cpp
   int64_t nSearchTime = txCoinStake.nTime & ~consensus.nStakeTimestampMask;
   if (nSearchTime > pwallet->m_last_coin_stake_search_time) {
       // ... actually run CreateCoinStake
       pwallet->m_last_coin_stake_search_time = nSearchTime;
   }
   ```
   This blocks re-entry within the same 16-second boundary.

3. **Boundary-aligned sleep helper** `MsUntilNextWindow()` (`miner.cpp`):
   ```cpp
   int64_t MsUntilNextWindow(const Consensus::Params& consensus, int64_t mtp)
   {
       int64_t now = GetAdjustedTimeSeconds();
       int64_t nextBoundary = (now & ~consensus.nStakeTimestampMask)
                            + (consensus.nStakeTimestampMask + 1);

       // Advance past MTP. Block timestamp must be strictly greater than MTP.
       while (nextBoundary <= mtp)
           nextBoundary += (consensus.nStakeTimestampMask + 1);

       return std::max(0LL, (nextBoundary - now) * 1000);
   }
   ```
   This computes the time until the next valid 16-second stake window, advancing past MTP if needed.

### 11.2 All sleeps go through `MsUntilNextWindow()`

`PoSMiner()` uses the helper for every PoS-related sleep:

| Path | Sleep |
|---|---|
| Failed coinstake (`fPoSCancel`) | `max(MsUntilNextWindow(consensus, pindexPrev->MTP), pos_timio)` |
| After successful block | `MsUntilNextWindow(consensus, newTip->MTP)` |
| Non-PoS fallback | `pos_timio` (unreachable for staker wallets) |

`pos_timio` remains as a CPU-throttling floor:

```cpp
pos_timio = gArgs.GetIntArg("-staketimio", DEFAULT_STAKETIMIO) + 30 * sqrt(vCoins.size());
```

### 11.3 No short-circuit path

The old safety-bump short-circuit that skipped `CreateNewBlock()` entirely has been removed. Every `cv_new_block` wake now enters `CreateNewBlock()`, which may quickly return `fPoSCancel` if the timer guard blocks the current window. The accepted trade-off is slightly more work per wake in exchange for a simpler, single-responsibility design.

### 11.4 Consequence: timing is computed at stake time

Because `MsUntilNextWindow()` is called inside `PoSMiner()` right before sleeping, the sleep duration is always computed from the **current** adjusted time and the **current** tip's MTP. There is no pre-calculated value that can become stale between block arrival and staker wake-up.

## The ExtractDestination Hack (addresstype.cpp:67-74)

Blackmore284 has a critical modification to `ExtractDestination` that bridges P2PK staking outputs to P2PKH address display:

```cpp
// Blackcoin: Reinterpret P2PK scripts as PKHash
// We need to do that because proof-of-stake mechanism uses P2PK outputs
// It partially reverts Bitcoin Core PR#28246
if (!pubKey.IsValid())
    return false;

addressRet = PKHash(pubKey);  // Converts P2PK → PKHash for display
return true;
```

### How it works

1. **P2PK script**: `<pubkey> OP_CHECKSIG`
2. **`ExtractDestination`**: Extracts pubkey, converts to `PKHash(pubKey)`
3. **`GetScriptForDestination(PKHash)`**: Returns P2PKH script: `OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG`
4. **Result**: P2PK outputs display as P2PKH addresses in the wallet UI

### Implications for P2PKH migration

**Current state (P2PK + hack)**:
- Staking output: P2PK script (`<pubkey> OP_CHECKSIG`)
- Wallet display: P2PKH address (via hack)
- Inconsistency: Script is P2PK, address is P2PKH

**If switching to P2PKH outputs**:
- Staking output: P2PKH script (`OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG`)
- Wallet display: P2PKH address (natural, no hack needed)
- Consistency: Script and address match

### Consensus verification impact

**Current `CheckBlockSignature` (validation.cpp:3869-3871)**:
```cpp
if (whichType == TxoutType::PUBKEY) {
    return CPubKey(vchPubKey).Verify(block.GetHash(), block.vchBlockSig);
}
```

This only accepts PUBKEY type. For P2PKH, it would need to:
1. Extract the hash from the P2PKH script
2. Find the pubkey that hashes to this value (requires keystore access or signature recovery)
3. Verify the signature

**Challenge**: `CheckBlockSignature` is a static function with no wallet/keystore access — it can't look up keys by hash.

### Alternative approach: OP_RETURN mechanism

Instead of P2PKH, use OP_RETURN for the block signing key (already implemented for descriptor wallets):
- vout[1]: `OP_RETURN <pubkey>` (non-spendable, stores signing key)
- vout[2]: P2PKH reward (spendable)

This avoids the P2PKH verification challenge while keeping P2PKH addresses for rewards.

## Peercoin Comparison

### Key Differences in Legacy Staking

| Component | Blackmore284 | Peercoin |
|-----------|--------------|----------|
| **ExtractDestination** | Has Blackcoin hack: P2PK → PKHash for display | Same hack: P2PK → PKHash (identical behavior) |
| **SignBlock location** | `node/miner.cpp:702-732` | `validation.cpp:4960-4990` |
| **CheckBlockSignature** | Accepts PUBKEY + OP_RETURN | Accepts PUBKEY only |
| **CreateCoinStake location** | `wallet/staking.cpp:251-518` | `wallet/wallet.cpp:3570-3966` |
| **Staking label** | `"SignKey"` (with `AddressPurpose::SIGNKEY`) | `"mintkey"` |
| **WITNESS_V1_TAPROOT handling** | Supported with bMinterKey | Supported with bMinterKey |
| **WITNESS_V0_KEYHASH handling** | Supported with bMinterKey | NOT supported (rejected) |

### ExtractDestination Differences

**Peercoin (`script/standard.cpp:245-251`)**:
```cpp
case TxoutType::PUBKEY: {
    CPubKey pubKey(vSolutions[0]);
    if (!pubKey.IsValid())
        return false;
    addressRet = PKHash(pubKey);  // Same as Blackmore284!
    return true;
}
```

**Blackmore284 (`addresstype.cpp:67-74`)**:
```cpp
// Blackcoin: Reinterpret P2PK scripts as PKHash
// We need to do that because proof-of-stake mechanism uses P2PK outputs
// It partially reverts Bitcoin Core PR#28246
if (!pubKey.IsValid())
    return false;
addressRet = PKHash(pubKey);  // Same code, but Blackcoin added comment
```

**Analysis**: Both codebases have identical behavior for PUBKEY extraction. The Blackcoin comment explains the "why" but the code is the same.

### CheckBlockSignature Differences

**Peercoin (`validation.cpp:4994-5010`)**:
```cpp
bool CheckBlockSignature(const CBlock& block)
{
    if (block.GetHash() == Params().GetConsensus().hashGenesisBlock)
        return block.vchBlockSig.empty();

    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.IsProofOfStake()? block.vtx[1]->vout[1] : block.vtx[0]->vout[0];

    if (Solver(txout.scriptPubKey, vSolutions) != TxoutType::PUBKEY)
        return false;

    const valtype& vchPubKey = vSolutions[0];
    CPubKey key(vchPubKey);
    if (block.vchBlockSig.empty())
        return false;
    return key.Verify(block.GetHash(), block.vchBlockSig);
}
```

**Blackmore284 (`validation.cpp:3857-3895`)**:
```cpp
static bool CheckBlockSignature(const CBlock& block)
{
    if (block.IsProofOfWork())
        return block.vchBlockSig.empty();

    if (block.vchBlockSig.empty())
        return false;

    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.vtx[1]->vout[1];
    TxoutType whichType = Solver(txout.scriptPubKey, vSolutions);

    if (whichType == TxoutType::PUBKEY) {
        std::vector<unsigned char>& vchPubKey = vSolutions[0];
        return CPubKey(vchPubKey).Verify(block.GetHash(), block.vchBlockSig);
    }
    else {
        // Block signing key also can be encoded in the nonspendable output
        // This allows to not pollute UTXO set with useless outputs e.g. in case of multisig staking
        const CScript& script = txout.scriptPubKey;
        CScript::const_iterator pc = script.begin();
        opcodetype opcode;
        std::vector<unsigned char> vchPushValue;

        uint256 hash = block.GetHash();

        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (opcode != OP_RETURN)
            return false;
        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (!IsCompressedOrUncompressedPubKey(vchPushValue))
            return false;
        return CPubKey(vchPushValue).Verify(hash, block.vchBlockSig);
    }

    return false;
}
```

**Key difference**: Blackmore284 added OP_RETURN support for descriptor wallets. Peercoin only supports PUBKEY type.

### CreateCoinStake Differences

**Kernel type handling**:
- Both convert PUBKEYHASH to P2PK output
- Both support PUBKEY (use kernel directly)
- Both support WITNESS_V1_TAPROOT with bMinterKey mechanism
- **Difference**: Peercoin groups WITNESS_V0_KEYHASH with PUBKEYHASH for conversion; Blackmore284 handles WITNESS_V0_KEYHASH separately

**Peercoin (`wallet.cpp:3699`)**:
```cpp
if (whichType == TxoutType::PUBKEYHASH || whichType == TxoutType::WITNESS_V0_KEYHASH)
```

**Blackmore284 (`staking.cpp:327`)**:
```cpp
if (whichType == TxoutType::PUBKEYHASH) // only PUBKEYHASH, not WITNESS_V0_KEYHASH
```

**Output layout**:
- **Peercoin**: Splits outputs based on RFC28 security level calculation
- **Blackmore284**: Simple split at 1000 COIN threshold

**Fee handling**:
- **Peercoin**: Calculates min fee based on transaction size, iterates to find optimal output count
- **Blackmore284**: No fee calculation for coinstake (free transactions)

### Wallet Format Support

Both support:
- **Legacy (BDB)**: `BerkeleyDatabase` in `wallet/bdb.h`
- **Descriptor (SQLite)**: `SQLiteDatabase` in `wallet/sqlite.h`

Wallet flag: `WALLET_FLAG_DESCRIPTORS` (bit 34) distinguishes between formats.

### SignBlock Implementation

**Peercoin** (`validation.cpp:4960-4990`):
- Located in validation.cpp (not miner.cpp)
- Identical logic to Blackmore284
- Both check `Solver(txout.scriptPubKey, vSolutions) != TxoutType::PUBKEY`
- Both have legacy vs descriptor paths

**Blackmore284** (`miner.cpp:702-732`):
- Same logic, different file location

### Implications for P2PKH Migration

1. **ExtractDestination**: Both already convert P2PK → PKHash for display. No change needed.

2. **CheckBlockSignature**: If switching to P2PKH outputs, must add P2PKH verification support. Peercoin doesn't have this, so Blackmore284 would need to implement it.

3. **Wallet compatibility**: Both support BDB and SQLite. P2PKH changes would affect both formats equally.

4. **RFC28 splitting**: Peercoin's more sophisticated output splitting might need adjustment if changing from P2PK to P2PKH outputs.
