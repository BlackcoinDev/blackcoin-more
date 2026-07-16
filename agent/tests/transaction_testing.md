# Transaction Testing: Multisig, CLTV, CSV on Blackcoin More v28.4.0

This document records a live mainnet test of advanced script types on Blackcoin More v28.4.0.

## Objective

Test that the v28.4.0 node can:

1. Create and spend **2-of-3 multisig** P2SH outputs.
2. Create and spend **CLTV** (absolute time-locked) P2SH outputs.
3. Create and spend **CSV** (relative time-locked) P2SH outputs.

The test was performed on a private mainnet node. Funds were sent from an external wallet, then spent back to a controlled return address.

## Test Wallet

A dedicated **legacy wallet** was created on the v28.4.0 node to hold the keys used inside the test scripts. Sensitive details (wallet name, RPC endpoint, keys, scripts, and addresses) are redacted in the checked-in files.

It is a non-descriptor (legacy) wallet because custom P2SH redeem scripts are easier to import with `importaddress` in legacy wallets.

## Files in this Directory

| File | Purpose |
|---|---|---|
| `blackmore_scripttest_legacy_setup.json` | Contains placeholder wallet name, RPC endpoint, keys, redeem scripts, and P2SH funding addresses used for the first test. |
| `blackmore_scripttest_manual_signed_v2.json` | Contains the manually signed CLTV spending transaction that passed mempool validation (signed hex redacted). |
| `blackmore_csv_correct_setup.json` | Setup data for the second, correctly-encoded CSV test (sensitive fields redacted). |
| `blackmore_csv_correct_result.json` | Confirmed spending transaction for the correctly-encoded CSV test (sensitive fields redacted). |
| `csv_funding_utxo.json` | The UTXO record for the corrected CSV funding transaction (txid, vout, amount, confirmations). |
| `sign_p2sh_custom.py` | First attempt at manual signing. Has a high-S signature mistake (does not enforce low-S); kept for reference only. |
| `sign_p2sh_custom_v2.py` | Production manual signer for CLTV/CSV-style P2SH outputs. Produces low-S signatures and tests them with `testmempoolaccept`. |
| `csv_test_sequences.py` | Tries different `nSequence` values for the first CSV spend to find one accepted by mempool policy. |
| `csv_bypass_attempt.py` | Attempts to broadcast the first (flawed) CSV spend directly to both v26.2.0 and v28.4.0 nodes. |
| `csv_correct_setup.py` | Generates a fresh key, builds a correctly minimal-encoded CSV P2SH address, and imports it watch-only. |
| `spend_csv_correct.py` | Builds, signs, tests, and broadcasts the spend for the correctly-encoded CSV output. |
| `transaction_testing.md` | This documentation file. |

## Funding Addresses

Three P2SH addresses were generated and funded with 1 BLK each. The actual addresses and redeem scripts are redacted from this checked-in copy.

| Test | P2SH Address | Redeem Script (hex) |
|---|---|---|
| CLTV | `<CLTV_P2SH_ADDRESS>` | `<CLTV_REDEEM_SCRIPT>` |
| CSV | `<CSV_P2SH_ADDRESS>` | `<CSV_REDEEM_SCRIPT>` |
| Multisig | `<MULTISIG_P2SH_ADDRESS>` | `<MULTISIG_REDEEM_SCRIPT>` |

### Decoded Scripts

- **CLTV**: `<lock_height> OP_CHECKLOCKTIMEVERIFY OP_DROP <pubkey> OP_CHECKSIG`
  - Locks until block height `5,900,000` (already passed at test time).
- **CSV**: `<delay> OP_CHECKSEQUENCEVERIFY OP_DROP <pubkey> OP_CHECKSIG`
  - Relative delay `0`. **Known flawed script** — see CSV result below.
- **Multisig**: `2 <pk1> <pk2> <pk3> 3 OP_CHECKMULTISIG`
  - 2-of-3 P2SH multisig.

## How to Read the JSON Files

### `blackmore_scripttest_legacy_setup.json`

Top-level fields:

- `wallet`: name of the wallet on the v28.4.0 node (redacted placeholder).
- `node`: RPC endpoint used (redacted placeholder).
- `cltv`: all data for the CLTV test.
  - `lock_height`: absolute block height lock (`5,900,000`).
  - `pubkey`: public key inside the script (redacted placeholder).
  - `pubkey_address`: the legacy address whose private key controls this pubkey (redacted placeholder).
  - `redeem_script_hex`: the full redeem script (redacted placeholder).
  - `p2sh_address`: the P2SH funding address (redacted placeholder).
- `csv`: same structure for CSV.
  - `delay`: relative delay value (`0`).
- `multisig`:
  - `required` / `total`: 2-of-3.
  - `pubkeys`: the three public keys (redacted placeholders).
  - `pubkey_addresses`: the three controlling legacy addresses (redacted placeholders).
  - `redeem_script_hex`: the full 2-of-3 multisig redeem script (redacted placeholder).
  - `p2sh_address`: the P2SH funding address (redacted placeholder).

### `blackmore_scripttest_manual_signed_v2.json`

A simple object mapping test name to the signed transaction hex that passed `testmempoolaccept`. The sensitive hex values are redacted in the checked-in copy.

In this run only CLTV produced a passing manual signature. Multisig was signed successfully by `signrawtransactionwithkey`, and the first CSV attempt failed (see Results).

### `blackmore_csv_correct_setup.json`

Setup data for the corrected CSV test:

- `wallet`: wallet name (redacted placeholder).
- `node`: RPC endpoint (redacted placeholder).
- `csv_correct`:
  - `delay`: relative locktime delay in blocks (`1`).
  - `pubkey`: public key inside the script (redacted placeholder).
  - `pubkey_address`: legacy address whose private key controls the pubkey (redacted placeholder).
  - `redeem_script_hex`: minimal-encoded CSV redeem script (redacted placeholder).
  - `p2sh_address`: funding address (redacted placeholder).
  - `note`: spending instructions.

### `blackmore_csv_correct_result.json`

Result of the corrected CSV spend:

- `funding_txid` / `funding_vout`: where the test coins came from.
- `spending_txid`: the confirmed transaction that returned the funds.
- `spending_hex`: full signed transaction hex (redacted placeholder).
- `redeem_script`: the CSV redeem script used (redacted placeholder).
- `nSequence`: the input sequence value (`1`).
- `fee` / `return_address`: fee paid and destination (redacted placeholder).

### `csv_funding_utxo.json`

The wallet's view of the corrected CSV funding UTXO, as returned by `listunspent`:

- `txid`, `vout`, `amount`, `confirmations`
- `scriptPubKey`: the P2SH scriptPubKey
- `spendable`/`solvable`: `false` because it is watch-only; the private key is held separately by the wallet address listed in the redacted setup file.

## Python Environment

### Required version

All scripts were written and tested with **Python 3.x** (specifically Python 3.14 in this environment). They should work with Python 3.8 or newer.

### Required libraries

- `ecdsa` — for secp256k1 private-key signing
- `requests` — for JSON-RPC calls to the Blackcoin node

### Installation

Create an isolated virtual environment and install the dependencies:

```bash
python3 -m venv /tmp/venv_btc
/tmp/venv_btc/bin/pip install ecdsa requests
```

### Running a script

```bash
/tmp/venv_btc/bin/python agent/tests/<script>.py
```

Example:

```bash
/tmp/venv_btc/bin/python agent/tests/spend_csv_correct.py
```

> ⚠️ **Security warning**: These scripts dump private keys from the test wallet and sign mainnet transactions. They are only safe because they were run against a dedicated test wallet with no other funds. Never run them against a wallet holding significant value.

### `sign_p2sh_custom_v2.py`

**Purpose**: Manually signs the CLTV and CSV spending transactions.

**What it does**:
1. Loads `blackmore_scripttest_legacy_setup.json`.
2. Fetches the private keys for the CLTV and CSV pubkey addresses from the wallet.
3. Builds raw transactions returning 0.999 BLK to a configured return address with a 0.001 BLK fee.
4. Computes the legacy Bitcoin sighash for P2SH (`SIGHASH_ALL`).
5. Signs with the `ecdsa` library, enforcing **low-S**.
6. Builds a P2SH `scriptSig` as `<sig> <redeem_script>`.
7. Calls `testmempoolaccept` on the v28.4.0 node.
8. Saves passing transactions to `blackmore_scripttest_manual_signed_v2.json`.

**Key parameters**:
- `NODE`, `WALLET`: RPC endpoint and wallet name (redact before sharing).
- `RETURN_ADDR`: destination address for returned funds (redact before sharing).
- `FEE`, `AMOUNT`: fee and output amount.
- `CLTV_LOCK_HEIGHT`: absolute block height used in the CLTV script.
- `CSV_DELAY`: relative delay used in the CSV script.

### `csv_test_sequences.py`

**Purpose**: Diagnose why the CSV spend is rejected by trying multiple `nSequence` values.

**What it does**:
- Rebuilds the CSV spend with `nSequence` values: `0`, `1`, `0x40000001`, `0x40000000`, `0x00010000`.
- Calls `testmempoolaccept` for each.
- Prints the rejection reason.

In this test, every sequence was rejected with:

```
non-mandatory-script-verify-flag (unknown error)
```

### `csv_bypass_attempt.py`

**Purpose**: Try to broadcast the CSV transaction directly to both v26.2.0 and v28.4.0 nodes without going through `testmempoolaccept`.

**What it does**:
- Builds the same CSV spend as `csv_test_sequences.py`.
- Calls `sendrawtransaction` on both the v26.2.0 and v28.4.0 test nodes.
- Prints the result of each attempt.

Both nodes rejected the transaction.

### `csv_correct_setup.py`

**Purpose**: Generate a fresh key and create a correctly minimal-encoded CSV P2SH address.

**What it does**:
1. Fetches a new legacy address and public key from the configured test wallet.
2. Builds the redeem script using `OP_1` (`0x51`) for a relative delay of 1 block.
3. Computes the P2SH address manually (matching `decodescript`).
4. Imports the P2SH address as watch-only.
5. Saves the setup to `blackmore_csv_correct_setup.json`.

**Output**: A single P2SH address to fund for the corrected CSV test.

### `spend_csv_correct.py`

**Purpose**: Spend the correctly-encoded CSV P2SH output.

**What it does**:
1. Loads `blackmore_csv_correct_setup.json`.
2. Fetches the private key for the CSV pubkey address.
3. Builds a raw spend with input `nSequence = 1` (matching the CSV delay).
4. Manually signs with low-S enforcement.
5. Calls `testmempoolaccept`.
6. Broadcasts with `sendrawtransaction`.
7. Saves the result to `blackmore_csv_correct_result.json`.

### `sign_p2sh_custom.py`

**Purpose**: First manual-signing attempt.

**Status**: **Deprecated**. It does not enforce low-S signatures. The CLTV transaction it produced was rejected with:

```
Non-canonical signature: S value is unnecessarily high
```

This is another example of a **test-script mistake**, not a node bug: the `ecdsa` library can produce high-S signatures, and Bitcoin/Blackcoin correctly reject them. `sign_p2sh_custom_v2.py` fixes this by reducing `s` to its low-S complement.

## Results

### Multisig ✅

- **Funding tx**: `<REDACTED>`
- **Spending tx**: `<REDACTED>`
- **Status**: Broadcast and confirmed.
- **Returned**: 0.999 BLK to a controlled return address.
- **Notes**: Signed with `signrawtransactionwithkey` using the three private keys from the test wallet. The wallet had all three keys, so it could produce both required signatures.

### CLTV ✅

- **Funding tx**: `<REDACTED>`
- **Spending tx**: `<REDACTED>`
- **Status**: Broadcast and confirmed.
- **Returned**: 0.999 BLK to a controlled return address.
- **Notes**: Signed manually with `sign_p2sh_custom_v2.py` because `signrawtransactionwithwallet` does not handle nonstandard CLTV redeem scripts. Lock height `5,900,000` was already passed, so the spend was valid immediately. Transaction `locktime` was set to `5,900,000` and input `nSequence` was `0xFFFFFFFE` (not final, so locktime is enforced).

### CSV ❌ (First Attempt — Test Construction Mistake)

- **Funding tx**: `<REDACTED>`
- **Funding vout**: `0`
- **Funding address**: `<REDACTED>`
- **Spending tx**: None broadcast.
- **Status**: **Stuck**.
- **Returned**: 0 BLK so far.

> **Important distinction**: The funding transaction is a normal, valid P2PKH→P2SH payment. It is correctly confirmed on-chain. The spending transaction — the one that would unlock the 1 BLK using the CSV redeem script — was never accepted by the mempool or mined into a block.

#### Why it failed

The mistake was in how the test redeem script was constructed. It was built as:

```asm
0100 b2 75 21 <pubkey> ac
```

`0100` pushes a 1-byte value `0x00`. This is a **non-minimal** encoding of the number `0`. The minimal encoding in Bitcoin Script is `00` (`OP_0`).

Both Bitcoin Core and Blackcoin enforce minimal encoding when `SCRIPT_VERIFY_MINIMALDATA` is active (standard/mempool policy). The interpreter correctly rejected the script with:

```
non-mandatory-script-verify-flag (unknown error)
```

This is **not a bug in Blackcoin or Bitcoin**; it is the expected behavior. The bug was in the test setup.

Because the redeem script is committed to by the P2SH hash, it **cannot be changed** after funding. The 1 BLK is therefore not spendable through normal `sendrawtransaction` on either v26.2.0 or v28.4.0.

#### Did the flawed CSV transaction end up in the mempool?

**No.** The flawed CSV transaction was rejected by both nodes and never entered either mempool. Here is the exact sequence:

| Step | Action | Result |
|---|---|---|
| 1 | `testmempoolaccept` on v28.4.0 | ❌ Rejected: `non-mandatory-script-verify-flag (unknown error)` |
| 2 | `testmempoolaccept` on v26.2.0 | ❌ Rejected: `non-mandatory-script-verify-flag (unknown error)` |
| 3 | `sendrawtransaction` on v28.4.0 | ❌ Rejected (HTTP 500) |
| 4 | `sendrawtransaction` on v26.2.0 | ❌ Rejected (HTTP 500) |

The 1 BLK remains unspent at the redacted funding address.

#### Is the CSV 1 BLK permanently lost?

No. Block-level consensus does **not** enforce `SCRIPT_VERIFY_MINIMALDATA`, so a miner could still include the transaction in a block. However, the test wallet has no staking weight, so it cannot mint a block itself.

#### How to recover

A staker/miner who trusts the raw transaction can include it in a block manually. The raw CSV spend hex that would be valid at consensus level was saved locally during the test and is not checked into this repository.

### CSV ✅ (Second Attempt — Correct Minimal Encoding)

A new test was run with a correctly minimal-encoded CSV script.

- **Funding address**: `<REDACTED>`
- **Funding tx**: `<REDACTED>`
- **Funding vout**: `1`
- **Redeem script**: `<REDACTED>`
  - Decoded: `1 OP_CHECKSEQUENCEVERIFY OP_DROP <pubkey> OP_CHECKSIG`
  - `51` = `OP_1` (minimal encoding of delay = 1 block)
- **Spending tx**: `<REDACTED>`
- **Input nSequence**: `1`
- **Status**: ✅ Broadcast and **confirmed** (8+ confirmations).
- **Returned**: 0.999 BLK to a controlled return address.
- **Notes**: After the funding tx had 1 confirmation, `spend_csv_correct.py` built a spend with `nSequence=1`, signed it manually with low-S enforcement, passed `testmempoolaccept`, and broadcast it successfully.

## Code Comparison with Bitcoin Core

The CSV minimal-data behavior is **identical** between Blackcoin More 28.4.0 and Bitcoin Core.

### `OP_CHECKSEQUENCEVERIFY`

Blackcoin More 28.4.0 (`src/script/interpreter.cpp`):

```cpp
const CScriptNum nSequence(stacktop(-1), fRequireMinimal, 5);
```

Bitcoin Core (`../bitcoin/src/script/interpreter.cpp`):

```cpp
const CScriptNum nSequence(stacktop(-1), fRequireMinimal, 5);
```

### `fRequireMinimal`

Both codebases set:

```cpp
bool fRequireMinimal = (flags & SCRIPT_VERIFY_MINIMALDATA) != 0;
```

### `CScriptNum` minimal encoding check

Both codebases throw `scriptnum_error("non-minimally encoded script number")` when `fRequireMinimal` is true and the operand has a non-minimal encoding such as `0100`.

Therefore the first CSV failure is **not a Blackcoin quirk** — it is the same behavior Bitcoin Core would exhibit under standard/mempool policy.

## Lessons Learned

1. **Use legacy wallets for custom P2SH scripts**: Descriptor wallets reject `importaddress` for nonstandard scripts.
2. **Always encode Script numbers minimally**:
   - Use `00` for the number `0` (`OP_0`), not `0100`.
   - Use `01<value>` only for values that cannot be represented by a single opcode (`OP_1`–`OP_16`).
3. **Low-S signatures are mandatory**: Both mempool policy and block-level `SCRIPT_VERIFY_LOW_S` require low-S DER signatures. The `ecdsa` library default can produce high-S; enforce reduction with `if s > N/2: s = N - s`. The rejection of high-S is correct node behavior, not a bug.
4. **Manual signing is required for nonstandard redeem scripts**: `signrawtransactionwithwallet` / `signrawtransactionwithkey` recognize standard multisig but not arbitrary CLTV/CSV patterns.
5. **Test on regtest/testnet first**: This would have caught the CSV minimal-data mistake before locking real mainnet coins.

## Correct CSV Script for Future Tests

For a relative delay of `N` blocks, the minimal redeem script is:

```
<N minimally encoded> OP_CHECKSEQUENCEVERIFY OP_DROP <pubkey> OP_CHECKSIG
```

Examples:

| Delay | Minimal script hex |
|---|---|
| 0 | `00b27521<pubkey>ac` |
| 1 | `51b27521<pubkey>ac` |
| 2 | `52b27521<pubkey>ac` |
| 16 | `60b27521<pubkey>ac` |
| 17 | `0111b27521<pubkey>ac` |

For values `1`–`16`, use opcodes `OP_1`–`OP_16` (`51`–`60`). For value `0`, use `OP_0` (`00`). For larger values, use the minimal CScriptNum push.

## Reproducing the Test

### Full test (multisig + CLTV + first CSV attempt)

1. Set up the Python environment:
   ```bash
   python3 -m venv /tmp/venv_btc
   /tmp/venv_btc/bin/pip install ecdsa requests
   ```
2. Ensure a legacy wallet exists on the v28.4.0 node, or create one:
   ```bash
   curl -u <rpcuser>:<rpcpassword> --data-binary '{"jsonrpc":"1.0","method":"createwallet","params":["<wallet_name>",false,false,"",false,false]}' -H 'content-type: text/plain;' http://<node>:<port>/
   ```
3. Generate keys, build scripts, and import P2SH addresses by running the setup portion of `sign_p2sh_custom_v2.py`.
4. Send small amounts to the three P2SH addresses.
5. Wait for confirmations.
6. Run `sign_p2sh_custom_v2.py` to sign and test CLTV/CSV spends.
7. For multisig, use `signrawtransactionwithkey` with the three private keys, as shown in the multisig section above.
8. Broadcast passing transactions with `sendrawtransaction`.

### Corrected CSV test only

1. Set up the Python environment (if not already done):
   ```bash
   python3 -m venv /tmp/venv_btc
   /tmp/venv_btc/bin/pip install ecdsa requests
   ```
2. Run `csv_correct_setup.py` to generate the P2SH funding address:
   ```bash
   /tmp/venv_btc/bin/python agent/tests/csv_correct_setup.py
   ```
3. Send funds to the printed P2SH address.
4. Wait for at least **1 confirmation**.
5. Run `spend_csv_correct.py` to sign and broadcast the spend:
   ```bash
   /tmp/venv_btc/bin/python agent/tests/spend_csv_correct.py
   ```

## Appendix: Key RPC Commands Used

```bash
# Create legacy wallet
curl -u <rpcuser>:<rpcpassword> -H 'content-type: text/plain;' \
  --data-binary '{"jsonrpc":"1.0","method":"createwallet","params":["<wallet_name>",false,false,"",false,false]}' \
  http://<node>:<port>/

# Get new legacy address and its pubkey
curl -u <rpcuser>:<rpcpassword> -H 'content-type: text/plain;' \
  --data-binary '{"jsonrpc":"1.0","method":"getnewaddress","params":["label","legacy"]}' \
  http://<node>:<port>/wallet/<wallet_name>

curl -u <rpcuser>:<rpcpassword> -H 'content-type: text/plain;' \
  --data-binary '{"jsonrpc":"1.0","method":"getaddressinfo","params":["<address>"]}' \
  http://<node>:<port>/wallet/<wallet_name>

# Import P2SH watch-only address
curl -u <rpcuser>:<rpcpassword> -H 'content-type: text/plain;' \
  --data-binary '{"jsonrpc":"1.0","method":"importaddress","params":["<p2sh>","label",false]}' \
  http://<node>:<port>/wallet/<wallet_name>

# Check UTXOs
curl -u <rpcuser>:<rpcpassword> -H 'content-type: text/plain;' \
  --data-binary '{"jsonrpc":"1.0","method":"listunspent","params":[1,999999,["<p2sh>"]]}' \
  http://<node>:<port>/wallet/<wallet_name>

# Test mempool acceptance
curl -u <rpcuser>:<rpcpassword> -H 'content-type: text/plain;' \
  --data-binary '{"jsonrpc":"1.0","method":"testmempoolaccept","params":[["<hex>"]]}' \
  http://<node>:<port>/

# Broadcast
curl -u <rpcuser>:<rpcpassword> -H 'content-type: text/plain;' \
  --data-binary '{"jsonrpc":"1.0","method":"sendrawtransaction","params":["<hex>"]}' \
  http://<node>:<port>/
```
