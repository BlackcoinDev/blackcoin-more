# v2 Coinstake — TxID Collision Analysis

## The Root Cause

Blackcoin v2 transactions drop `nTime` from serialization:

```cpp
// transaction.h
if (tx.version < 2)
    s << tx.nTime;   // v1: serialized (4 bytes)
else
    tx.nTime = 0;     // v2: stripped
```

The same check exists in **both** sighash paths:

- **Legacy sighash** (`interpreter.cpp:1333-1335`): `CTransactionSignatureSerializer` only serializes `nTime` if `version < 2`
- **BIP143 (segwit)** (`interpreter.cpp:1609-1612`): same — only includes `nTime` if `version < 2`

Since `nTime` is excluded from the sighash, the message being signed is identical every time → RFC6979 deterministic signatures produce the **same signature** → `scriptSig` is the same → the serialized non-witness bytes are identical → **same txid**.

This affects **all** single-input v2 coinstakes regardless of wallet type (legacy/descriptor) or address type (P2PKH/P2WPKH/P2TR).

```
v2 serialization:  [version=2][vin: {prevout, scriptSig=(same sig), sequence}][vout][nLockTime=0]
                      ↓                         ↓                               ↓           ↓
                   always 2            identical each attempt          identical each attempt   0
```

## When TxID Collides

Same UTXO staked twice at different times in a v2 coinstake:

| Field | Attempt 1 (T1) | Attempt 2 (T2) | Same? |
|---|---|---|---|
| `version` | 2 | 2 | Yes |
| `vin[0].prevout` | UTXO | UTXO | Yes |
| `vin[0].scriptSig` | sig | sig (RFC6979, same message) | **Yes** |
| `vin[0].nSequence` | same | same | Yes |
| `vout[].scriptPubKey` | same destination | same destination | Yes |
| `vout[].nValue` | same reward (same height) | same reward (same height) | Yes |
| `nLockTime` | 0 | 0 | Yes |
| **Non-witness bytes** | — | — | **Identical** |
| **TxID** | X | **X (same!)** | **Yes ✗** |
| `nTime` (in memory) | T1 | T2 | No (but stripped from serialization & sighash) |
| Signature (witness for segwit, scriptSig for legacy) | commits to T1 | commits to T2 | No (but sighash excludes nTime for v2 → same message) |

**This is a universal property of v2 coinstakes** — not specific to segwit or any wallet type.

## Historical Misconception

Previous versions of this doc incorrectly claimed legacy (P2PKH) coinstakes avoided the collision because the signature goes in `scriptSig` and "varies per attempt." This is wrong:

1. For v2, **both** legacy and BIP143 sighash exclude `nTime`
2. Same transaction bytes → same sighash → RFC6979 → same signature → same `scriptSig` → same txid
3. The wallet type (legacy BDB vs descriptor SQLite) is irrelevant — both use the same sighash rules
4. `SignTransaction` zeroing `nTime` at `sign.cpp:823` is redundant for v2 (the legacy `CTransactionSignatureSerializer` also skips it at `interpreter.cpp:1333`)

## When This Actually Happens

1. Local node stakes block at height H with coinstake txid=X
2. Block gets orphaned (competitor's heavier chain wins)
3. UTXO becomes spendable again
4. Wallet retries same UTXO at next window
5. Creates new coinstake with **same txid=X** (all v2 coinstakes from same UTXO have same txid)
6. Two different blocks contain transactions with the same txid

## Qtum Comparison (Why They Don't Have This Problem)

Qtum has the **exact same deterministic-txid issue** — no `nTime` field in transactions at all (`transaction.h:210-263`), v2 default (`CURRENT_VERSION=2`). But Qtum's consensus **doesn't rely on txid uniqueness** for staking.

Qtum's block header (`block.h:25-42`):
```
nVersion, hashPrevBlock, hashMerkleRoot, nTime, nBits, nNonce,
hashStateRoot, hashUTXORoot, prevoutStake, vchBlockSigDlgt
```

The `prevoutStake` field (`COutPoint`) identifies which UTXO is being staked. Combined with `block.nTime`, each stake has a unique `(prevoutStake, nTime)` pair at the **block header level**.

Qtum's `setStakeSeen` (`validation.cpp:136, 6305`) is a `std::set<std::pair<COutPoint, unsigned int>>` keyed by `(prevoutStake, block.nTime)`. Before accepting a new header, it checks:

```cpp
if (header.IsProofOfStake() &&
    setStakeSeen.count({header.prevoutStake, header.nTime}) &&
    !BlockIndex().count(header.GetHash()))
    return error("dupe-stake");
```

On reorg: the orphaned block's coinstake (same txid) is removed from UTXO set. The new block's coinstake (same txid, different `block.nTime`) passes the `setStakeSeen` check because `(prevoutStake, nTime_new)` hasn't been seen. The txid collision is irrelevant — the stake is uniquely identified by the block header.

| | Blackcoin (current) | Qtum |
|---|---|---|
| Transaction `nTime` | Only in v1; stripped in v2 | Nonexistent |
| TxID deterministic for v2 coinstakes? | **Yes — always** | **Yes — same** |
| `prevoutStake` in block header | No | **Yes** |
| `setStakeSeen` anti-dupe | No | **Yes** — `(prevoutStake, block.nTime)` |
| Problem exists? | **Yes** (no mitigation) | **No** (consensus-level uniqueness via header) |

## Potential Consequences for Blackcoin

### 1. Block relay / peer handling
- Orphaned tx still in some peer's recent-replay cache
- New block with same txid triggers "already have tx" logic
- **Severity**: Low. Peers handle duplicate txids at block level via block hash.

### 2. Wallet tracking
- `AddToWallet` sees same txid from orphaned block and new block
- Could confuse confirmation status tracking
- **Severity**: Medium. Wallet might think tx was already confirmed from orphan.

### 3. UTXO set
- Orphan disconnect removes old outputs; new block adds same outpoints
- `AddCoin` treats this as overwrite (coinbase txs are always overwritable)
- **Severity**: Low. UTXO set handles this gracefully.

### 4. CoinStatsIndex
- Same outpoints processed twice across reorg
- MuHash insert/remove should cancel correctly (same serialized data)
- **Severity**: Low. Index handles reorgs with `ReverseBlock` + `CustomAppend`.

### 5. StakeSeen (duplicate stake prevention)
- No equivalent of Qtum's `setStakeSeen`
- Without this, a reorg could replay the same `(prevout, blockTime)` pair
- **Severity**: Medium-High for reorg scenarios.

## Viable Solutions

### Option A: Qtum's `setStakeSeen` (no fork, ~20 lines)

Add a set of `(COutPoint, uint32_t)` pairs recording every accepted coinstake kernel:

```cpp
std::set<std::pair<COutPoint, unsigned int>> setStakeSeen;
```

Check in `AcceptBlockHeader`:
```cpp
if (block.IsProofOfStake()) {
    auto key = std::make_pair(block.vtx[1]->vin[0].prevout, block.nTime);
    if (setStakeSeen.count(key))
        return error("Duplicate stake: same prevout and time");
    setStakeSeen.insert(key);
}
```

**Pros**: Simple, Qtum-proven, no serialization or consensus changes.
**Cons**: Doesn't prevent txid collision itself — prevents duplicate acceptance at runtime. Memory for the set (~negligible).

### Option B: `prevoutStake` in block header (soft fork)

Include the kernel prevout in the block header (like Qtum). Commits the kernel input to the block hash. Enables delegation.

**Pros**: Stronger guarantee (block hash changes too), enables delegation.
**Cons**: Larger change across serialization, validation, mining. Soft fork.

### Option C: `nTime` back in v2 serialization (hard fork — DO NOT)

Re-adding `nTime` to v2 serialization changes every v2 txid. Old nodes deserialize `nTime` bytes as the start of `vin` → permanent chain split. Not viable.

### Option D: Do nothing

| Concern | Risk | Why |
|---|---|---|
| Block relay | **Low** | Blocks identified by hash, not txid |
| UTXO set | **Low** | Orphan disconnect removes; reconnect adds. Coinbase overwrite is normal. |
| Wallet tracking | **Medium** | Same txid from orphan + new block could confuse state |
| CoinStatsIndex | **Low** | MuHash insert/remove cancel correctly |
| Peer mempool | **Low** | Coinstakes bypass mempool (go directly into blocks) |

The collision requires: v2 coinstake (default) + same UTXO + same height (reward identical) + orphan + retry. Edge case of an edge case.

## Key Files

- `src/primitives/transaction.h:235-236, 277-278` — nTime stripped for v2
- `src/script/interpreter.cpp:1333-1335` — legacy sighash also strips nTime for v2
- `src/script/interpreter.cpp:1609-1612` — BIP143 sighash also strips nTime for v2
- `src/script/sign.cpp:823-824` — `SignTransaction` zeroes nTime (redundant for v2)
- `src/wallet/staking.cpp:496-519` — coinstake signing path
- `../qtum/src/validation.cpp:136, 6305` — Qtum's `setStakeSeen`
- `../qtum/src/primitives/block.h:25-42` — Qtum's `prevoutStake` in header
