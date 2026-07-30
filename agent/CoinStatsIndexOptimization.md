# CoinStatsIndex — Blackcoin Coinstake Awareness Specification

## Overview

Blackcoin's `CoinStatsIndex` extends Bitcoin Core's index to be fully coinstake-aware for Proof-of-Stake (PoS). It tracks `fCoinStake` alongside `fCoinBase` in both the MuHash calculation and UTXO stats, without modifying consensus rules or introducing non-deterministic elements into the hash.

---

## Technical Implementation

### 1. MuHash Encoding (`kernel/coinstats.cpp`)

`TxOutSer` encodes `fCoinBase` in bit 0 and `fCoinStake` in bit 1, with `nHeight` shifted left by 2 bits:

```cpp
ss << static_cast<uint32_t>((coin.nHeight << 2) | (coin.fCoinBase ? 1u : 0u) | (coin.fCoinStake ? 2u : 0u));
```

**Key Design Properties:**
- **No `nTime` in MuHash:** `nTime` is excluded from the MuHash calculation. This ensures the hash remains strictly deterministic regardless of whether transaction inputs are read from memory, active chain state, or disk undo data (`rev*.dat`).
- **Structural Properties Only:** `fCoinStake` is a structural property derived from the transaction (`tx->IsCoinStake()`), making it immutable and consistent across all nodes.
- **Bit Allocation:**
  - Bit 0: `fCoinBase`
  - Bit 1: `fCoinStake`
  - Bits 2+: `nHeight`

---

### 2. UTXO Statistics Tracking

#### Data Structures

`CCoinsStats` (`kernel/coinstats.h`) and `CoinStatsIndex` (`index/coinstatsindex.h`) track coinstake rewards separately from coinbase:

| Location | Field / Member | Type | Purpose |
|---|---|---|---|
| `kernel/coinstats.h` | `total_coinstake_amount` | `std::optional<CAmount>` | Cumulative gross coinstake output value (`std::optional` for overflow safety) |
| `kernel/coinstats.h` | `total_coinstake_input_amount` | `std::optional<CAmount>` | Cumulative value of inputs spent by coinstake transactions |
| `index/coinstatsindex.h` | `m_total_coinstake_amount` | `CAmount` | Running total of coinstake outputs in index |
| `index/coinstatsindex.h` | `m_total_coinstake_input_amount` | `CAmount` | Running total of spent coinstake inputs in index |
| `index/coinstatsindex.cpp` | `DBVal::total_coinstake_amount` | `CAmount` | Persistent database record |
| `index/coinstatsindex.cpp` | `DBVal::total_coinstake_input_amount` | `CAmount` | Persistent database record |

#### Transaction Categorization (`CustomAppend` & `ReverseBlock`)

During block processing, outputs are split into three explicit categories:
1. `tx->IsCoinBase()` → Coinbase output (`m_total_coinbase_amount`)
2. `tx->IsCoinStake()` → Coinstake reward output (`m_total_coinstake_amount`)
3. Regular transaction → New output (`m_total_new_outputs_ex_coinbase_amount`)

Inputs spent by coinstake transactions are accumulated in `m_total_coinstake_input_amount`.

---

### 3. Subsidy Accounting & Unclaimed Rewards

#### Pre-PoSv3 Subsidy Calculation

Pre-PoSv3 PoS blocks used coin-age rewards rather than fixed protocol subsidies. For these blocks, `CustomAppend` calculates effective block subsidy dynamically based on coinstake deltas:

```cpp
CAmount effective_subsidy;
if (block.is_pos && !Params().GetConsensus().IsProtocolV3(block.data->nTime)) {
    effective_subsidy = (m_total_coinstake_amount - coinstake_before) -
                        (m_total_coinstake_input_amount - coinstake_input_before);
} else {
    effective_subsidy = block_subsidy;
}
m_total_subsidy += effective_subsidy;
```

For PoW blocks and PoSv3+ blocks, standard protocol `block_subsidy` is used directly.

#### Genesis Block Handling

Blackcoin's genesis coinbase output is 0 BLK. Rather than adding theoretical subsidy to unspendable metrics, `CustomAppend` walks genesis outputs directly to count unspendable scripts.

---

### 4. Direct Path vs Index Path

- **CoinStatsIndex Path (`LookUpStats`)**: Supplies all extended metrics (`total_coinstake_amount`, `total_coinstake_input_amount`, `total_subsidy`, `total_unspendable_amount`).
- **Direct UTXO Scan (`ComputeUTXOStats`)**: Evaluates `total_coinstake_amount` using `ApplyStats` and the per-coin `fCoinStake` flag. `total_coinstake_input_amount` is unavailable in direct scans as it requires historical block undo records.

---

### 5. RPC Integration (`rpc/blockchain.cpp`)

The `gettxoutsetinfo` RPC exposes net coinstake rewards under `block_info.coinstake`:

```json
{
  "block_info": {
    "prevout_spent": "...",
    "coinbase": "...",
    "coinstake": "(coinstake output delta) - (coinstake input delta)",
    "new_outputs_ex_coinbase": "...",
    "unspendable": "...",
    "unspendables": {
      "genesis_block": "...",
      "scripts": "...",
      "unclaimed_rewards": "..."
    }
  }
}
```

The net reward per block is defined as: `(coinstake_output_delta) - (coinstake_input_delta)`.

---

### 6. Testing (`test/coinstatsindex_tests.cpp`)

`coinstatsindex_coinstake_awareness` unit test verifies:
- 2-bit flag encoding (`fCoinBase` = bit 0, `fCoinStake` = bit 1, height = bits 2+).
- Distinct hash codes for coinbase, coinstake, and regular outputs at identical heights.
- Correct behavior at block height 0.
- `GetBogoSize` output consistency (4 bytes allocated for height + flags).

---

## Comparison with Bitcoin Core Upstream (v28.4.0)

| Feature | Bitcoin Core v28.4 | Blackcoin bdev |
|---|---|---|
| `Coin` has `fCoinStake`? | No | **Yes** |
| `Coin` has `nTime`? | No | **Yes** |
| MuHash Height Encoding | `(height << 1) \| fCoinBase` | `(height << 2) \| fCoinBase \| (fCoinStake << 1)` |
| MuHash Compatibility | Upstream standard | **Custom** (2 flag bits) |
| Coinstake Reward Stats | N/A | **Tracked** (output & input totals) |
| Pre-PoSv3 Reward Calculation | N/A | **Dynamic coin-age subsidy calculation** |
| BIP30 Unspendables Tracked | Yes | **No** (omitted; no BIP30 collisions) |

---

## Downstream Maintenance Strategy

Blackcoin maintains compatibility with Bitcoin Core upstream releases via downstream merging/rebasing.

- **Upstream Rebase Isolation:** All PoS coinstake customizations (`fCoinStake` flag in `TxOutSer`, `effective_subsidy`, coinstake stats tracking, genesis output walk) are isolated in `coinstatsindex` and `kernel/coinstats`.
- **Forward-Porting Awareness:** When upgrading to future Bitcoin Core versions (e.g., Core 29/30+ where `BlockInfo` interface and `CustomOptions` are introduced), the customized PoS encoding (`<< 2`) and stats fields will need to be re-applied on top of the newer upstream index framework.
