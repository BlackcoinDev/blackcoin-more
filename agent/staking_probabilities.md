# Staking Probabilities and UTXO Analysis

## Summary

For Blackcoin More PoS staking, the expected reward rate depends on **total wallet weight relative to network weight**. The distribution of UTXO sizes only affects variance, cooldown dynamics, and CPU overhead — not the long-term expected rewards.

---

## 1. Kernel Hash Probability

The PoS kernel check (`pos.cpp:CheckStakeKernelHash`) is:

```
SHA256(nStakeModifier + txPrev.nTime + prevout.hash + prevout.n + nTimeTx) < bnTarget * amount
```

For a single UTXO of `amount` in a single 16-second window:

```
P(success per window) ≈ amount * target / 2^256
```

Where:
- `target` is derived from current `nBits`
- `amount` is in satoshis
- Each UTXO gets one hash attempt per 16-second window

### 1.1 Minimum Input Size (current mainnet conditions)

From recent mainnet conditions (`nBits` ≈ `1a0efc08`, PoS difficulty ≈ 1,120,000):

| Chance per 16s window | Required BLK |
|---|---|
| 0.001% | ~480 BLK |
| 0.01% | ~4,800 BLK |
| 0.1% | ~48,000 BLK |
| 1% | ~480,000 BLK |
| 50% | ~24,000,000 BLK |

These values scale linearly with difficulty. If the network difficulty were 800,000–960,000 instead, the 0.001% row would be ~340–410 BLK.

UTXOs below ~100–200 BLK have near-zero practical chance in any reasonable timeframe at current difficulty.

---

## 2. Total Wallet Weight vs Per-UTXO Chances

The total probability of finding a block in one window is the sum of per-UTXO probabilities:

```
P(total) ≈ Σ (amount_i * target / 2^256) = total_weight * target / 2^256
```

**Example configurations with identical total weight:**

| Configuration | Total Weight | Expected Reward Rate |
|---|---|---|
| 1000 × 1000 BLK | 1,000,000 BLK | identical |
| 2000 × 500 BLK | 1,000,000 BLK | identical |
| 4000 × 250 BLK | 1,000,000 BLK | identical |

The **total weight** determines the expected reward rate. UTXO distribution only affects:
- **Variance** (fewer large UTXOs = higher variance, more smaller = lower variance)
- **Cooldown impact** (when a UTXO stakes, it is locked for ~500 blocks)
- **CPU load** (more UTXOs = more hash attempts per window)
- **Coinstake tx size** (split behavior)

---

## 3. Expected Time to Find a Block

The RPC `getstakinginfo` reports:

```json
{
  "difficulty": 801060.5454231506,
  "weight": 79557355306641,
  "netstakeweight": 1086262969416965,
  "expectedtime": 913,
  "search-interval": 16
}
```

### 3.1 Formula

Implemented in `src/wallet/rpc/staking.cpp:76`:

```cpp
uint64_t nExpectedTime = staking ? 1.0455 * nTargetSpacing * nNetworkWeight / nWeight : 0;
```

Where:
- `nTargetSpacing = 64` seconds (`src/kernel/chainparams.cpp:114`)
- `1.0455` = hardcoded orphan-rate correction factor (~4.55%)
- `nNetworkWeight / nWeight` = inverse of your network share

### 3.2 Verification

For the example above:

```
nNetworkWeight / nWeight = 10,862,629 / 795,574 ≈ 13.65
1.0455 * 64 * 13.65 ≈ 913 seconds
```

Matches the RPC output.

### 3.3 Actual Network Orphan Rate

Based on user observation from `chainz.cryptoid.info/blk/orphans.dws`:

- ~40 stale/orphan blocks per day
- ~1,280 successful blocks per day
- Total attempts ≈ 1,320/day

**Actual orphan rate**: 40 / 1,320 ≈ **3.0%**

**Effective block time**: 86,400 / 1,280 ≈ **67.5 seconds**

This is slightly higher than the hardcoded 1.0455 factor (4.55%) because it also includes:
- Network propagation latency
- Clock drift between nodes
- Minor variance in stake modifier timing

For a more accurate GUI/RPC estimate, the multiplier would need to be closer to `67.5 / 64 ≈ 1.0547`.

### 3.4 Qt GUI vs RPC

Both use the **same formula** (`src/qt/bitcoingui.cpp:1599` vs `src/wallet/rpc/staking.cpp:76`):

```cpp
1.0455 * nTargetSpacing * nNetworkWeight / nWeight
```

Minor differences:
- RPC returns `uint64_t seconds`
- GUI returns `unsigned` seconds and formats it as a human-readable string

For practical purposes the estimates are identical.

---

## 4. UTXO Selection Behavior

### 4.1 Why some UTXOs are reused and others never stake

The wallet selects eligible UTXOs in `src/wallet/staking.cpp:SelectCoinsForStaking` and `AvailableCoinsForStaking`:

- UTXOs are returned in wallet map order (`mapWallet`, keyed by txid), not sorted by value
- `CreateCoinStake` iterates them in that deterministic order
- Each UTXO's kernel hash is tested against the current 16-second window

Combined with:
- 16-second timer guard (`nStakeTimestampMask = 0xf`)
- 500-block cooldown after a successful stake

This means:
- **Large UTXOs** have a higher per-attempt chance to clear `bnTarget * amount`, so they win more often when reached
- **Small UTXOs** rarely clear the threshold and can appear to never stake
- **Repeated UTXOs** in logs are typically the ones that successfully passed the kernel check in consecutive windows

### 4.2 This is local to the wallet

- Coin selection is local (only your UTXOs)
- Kernel search is local
- Other wallets only affect **difficulty** and **stake modifier**
- They do not influence which of your UTXOs is tested

---

## 5. StakeCombineThreshold and Split Behavior

`GetStakeCombineThreshold()` is hardcoded in `src/wallet/staking.cpp:16`:

```cpp
static int64_t GetStakeCombineThreshold() { return 250 * COIN; }
static int64_t GetStakeSplitThreshold() { return 2 * GetStakeCombineThreshold(); }
```

The **combine** logic at `src/wallet/staking.cpp:419` stops adding more inputs once the running credit reaches the combine threshold:

```cpp
if (nCredit >= GetStakeCombineThreshold()) {
    // stop combining inputs
}
```

The actual **split** logic is at `src/wallet/staking.cpp:455` and uses the split threshold:

```cpp
if (nCredit >= GetStakeSplitThreshold()) {
    // split reward into 2 outputs
}
```

So splitting happens at **500 BLK**, not 250 BLK.

### 5.1 Adapting threshold to input size

If you changed the **split** threshold to match input size (e.g., 250 BLK for 500 BLK inputs), every generation will split once, producing 2 equal outputs:

| Input | Split Threshold | Reward | Children |
|---|---|---|---|
| 1000 | 1000 | 1000 | 500 + 500 |
| 500 | 500 | 500 | 250 + 250 |
| 250 | 250 | 250 | 125 + 125 |

But **125 would still split** because the condition is `>=`:

| Input | Condition | Children |
|---|---|---|
| 125 | `125 >= 125` = true | 62.5 + 62.5 |

To stop at 125, you need strict inequality (`>`) or add a floor check.

### 5.2 Why splitting doesn't change total expected rewards

Splitting preserves total weight. The total expected reward rate depends on sum of UTXO values, not count.

---

## 6. Network Timing Edge Cases

### 6.1 +14 second clock drift advantage

`FutureDrift` at `src/validation.cpp:145`:

```cpp
return Params().GetConsensus().IsProtocolV2(nTime) ? nTime + 15 : nTime + 10 * 60;
```

Protocol V2 accepts blocks up to **15 seconds in the future**. A staker with +14s clock drift can hash the next 16s boundary ~14s before honest nodes, gaining a head start. This is within protocol tolerance but gives a measurable advantage.

### 6.2 Multi-node staking with same wallet

Two nodes staking the same wallet with different `stakecombinethreshold` produce different coinstake transactions (different `txid`) for the same kernel. Only one can win because both spend the same input. This causes self-competition.

To avoid this:
- Use identical settings on all nodes
- Or run only one active staker per wallet
- Or partition UTXOs across wallets

---

## 7. Related Code Changes

### 7.1 CoinStatsIndex MuHash Fix

`src/kernel/coinstats.cpp` was modified to make the UTXO stats hash PoS-aware:

```cpp
ss << outpoint;
ss << static_cast<uint32_t>((coin.nHeight << 2) + (coin.fCoinBase ? 1u : 0u) + (coin.fCoinStake ? 2u : 0u));
ss << VARINT(coin.nTime);
ss << coin.out;
```

- Includes `fCoinStake` and `nTime` in the MuHash
- Aligns Blackcoin's UTXO set commitment with PoS state
- Requires rebuilding the `coinstats` index DB

See `agent/staking.md` §10 and the Qtum comparison for more details.

---

## 8. Key Takeaways

1. **Total wallet weight** determines expected reward rate, not UTXO count.
2. **UTXO distribution** affects variance, cooldown, and CPU load.
3. **Minimum effective stake** at current difficulty is ~480 BLK for a 0.001% chance per 16s window.
4. **Orphan rate** affects expected time between blocks but not per-attempt probability.
5. **Expected time formula** `1.0455 * 64 * net_weight / my_weight` matches RPC/GUI but uses a hardcoded ~4.55% orphan estimate; actual observed rate is closer to ~3% + latency.
6. **Deterministic coin selection** walks UTXOs in wallet order; large UTXOs have a higher per-window chance, so they appear more often in logs while small ones rarely clear the threshold.
7. **Clock drift up to 15s** is valid and can be exploited for a staking advantage.
8. **Multi-node setups** with mismatched settings cause self-orphans.
9. **All sleeps are boundary-aligned and MTP-aware** — `PoSMiner()` uses `MsUntilNextWindow()` to sleep until the next valid 16-second stake timestamp, advancing past MTP if needed. There is no pre-calculated safety bump; timing is computed at stake time.

---

## 9. Useful RPCs

```bash
# Current staking status
blackmore-cli getstakinginfo

# Current PoS difficulty
blackmore-cli getdifficulty

# UTXO set stats (slow without index)
blackmore-cli gettxoutsetinfo muhash false

# Index status
blackmore-cli getindexinfo
```
