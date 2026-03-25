# Functional Tests — Agent Knowledge Base

**Scope**: `test/functional/` | **Score**: 18 (225 files, Python tests)

---

## ⚠️ CRITICAL: Bitcoin 26.x → 30.x Upgrade Context

**Current Phase**: See .sisyphus/plans/bitcoin-28.4.0-merge-plan.md v2.7

### Test Status for Blackcoin More Features

| Test Category | Bitcoin Core | Blackcoin More | Status |
|---------------|--------------|----------------|--------|
| RBF tests | Enabled | **DISABLED** | Should be skipped |
| Fee estimation tests | Dynamic | **Static** | Should be skipped |
| Taproot tests | Active | **Never active** | Should be skipped |
| BDB wallet tests | Removed | **Required** | Must pass |
| PoS staking tests | N/A | **Required** | Manual testing |
| SegWit tests | Active | **Active (March 2026)** | Full support |

### Tests to Skip (Blackcoin More Specific)

**NEVER ADAPTED - Skip or Expect Failures**:

1. **RBF Tests** (Completely Disabled)
   - `feature_rbf.py` - RBF functionality
   - `mempool_reorg.py` - RBF during reorg
   - `mempool_package_onemore.py` - Package RBF
   - Skip command: `--skip=*rbf*`

2. **Fee Estimation Tests** (Static Fees)
   - `fee_estimation.py` - Dynamic fee estimation
   - `p2p_feefilter.py` - Fee filter with estimation
   - Skip command: `--skip=*fee*`

3. **Taproot Tests** (Never Active)
   - `feature_taproot.py` - Taproot activation
   - `feature_notifications.py` - Taproot notifications
   - Skip command: `--skip=*taproot*`

### Tests That MUST PASS (Blackcoin More Critical)

1. **BDB Wallet Tests** (BDB 6.2 Required)
   - `wallet_backup.py` - BDB backup/restore
   - `wallet_upgradewallet.py` - Wallet upgrade
   - `wallet_importpruned funds.py` - Import functionality

2. **PoS Staking Tests** (Manual Verification Required)
   - `wallet_staking.py` - Staking functionality
   - `mining_pos.py` - PoS block generation
   - **NOTE**: These tests are NOT adapted for PoS - manual verification required

3. **Static Fee Tests** (Fixed 100,000 sat/kvB)
   - `wallet_send.py` - Send with static fees
   - `wallet_sendmany.py` - Batch sends

### Test Configuration for Blackcoin More

```bash
# Skip Bitcoin-specific tests
test/functional/test_runner.py \
    --skip='*rbf*|*fee_estimation*|*taproot*|*descriptor*' \
    --verbose

# Run only Blackcoin More relevant tests
test/functional/test_runner.py \
    wallet_backup.py \
    wallet_upgradewallet.py \
    wallet_staking.py \
    --verbose
```

### Anti-Patterns (THIS MODULE)

- **NEVER**: Attempt to fix all functional tests to pass
- **NEVER**: Trust test results for PoS validation
- **NEVER**: Use as sole validation (manual required)
- **NEVER**: Port Bitcoin Core's RBF tests
- **NEVER**: Port Bitcoin Core's fee estimation tests
- **NEVER**: Port Bitcoin Core's Taproot tests

### Testing Reality

- **RBF tests**: Skip completely (functionality disabled)
- **Fee estimation tests**: Skip completely (static fees only)
- **Taproot tests**: Skip completely (never active)
- **BDB wallet tests**: MUST pass (BDB 6.2 required)
- **PoS staking tests**: Manual verification required
- **SegWit tests**: Partial (testnet active, mainnet in progress)

---

## Overview

Python unittest-based integration tests. **NEVER ADAPTED for Blackcoin's PoS**. Most tests fail or are incompatible. For staking, transactions, sync → manual testing required.

---

## Structure

```
test/functional/
├── test_runner.py            # Main test orchestrator
├── test_framework/           # Shared test utilities
│   ├── p2p.py                # P2P message handling
│   ├── script.py             # Script validation
│   ├── blocktools.py         # Block construction
│   └── util.py               # Test helpers
├── wallet_*.py               # Wallet tests
├── rpc_*.py                  # RPC tests
├── feature_*.py              # Feature tests
└── mempool_*.py              # Mempool tests
```

---

## ⚠️ CMake Migration (29.x → 30.x)

**Bitcoin Core 30.x migrated from Autotools to CMake.**

**When migrating test infrastructure to CMake**:
- Preserve RBF test skipping (functionality disabled)
- Preserve fee estimation test skipping (static fees only)
- Preserve BDB wallet test execution (BDB 6.2 required)
- Never port Bitcoin Core's RBF/fees/taproot tests

**Reference**: `agent/CMake_MIGRATION.md` for complete build system migration details.

---

## Important Notes

- **Unadapted**: Tests inherit Bitcoin Core patterns, not Blackcoin PoS
- **Expect failures**: Do not file bugs for test failures
- **Manual testing**: Required for core PoS functionality
- **Caching**: `--cached` flag reuses blockchain cache

---

## Commands

```bash
# Run all tests
test/functional/test_runner.py

# Single test
test/functional/test_runner.py wallet_upgradewallet.py

# Verbose
test/functional/test_runner.py --verbose <test>

# Category
test/functional/test_runner.py --cached wallet_*.py

# Failfast
test/functional/test_runner.py --verbose --failfast <test>
```

---

## Anti-Patterns (THIS MODULE)

- **NEVER**: Attempt to fix functional tests to pass
- **NEVER**: Trust test results for PoS validation
- **NEVER**: Use as sole validation (manual required)

---

## Testing Reality

- Higher testing effort expected
- Manual validation of staking required
- Transaction confirmations must be verified manually
- Blockchain sync testing done manually

---

*Generated by /init-deep*
