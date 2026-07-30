# Assert Risk Analysis

## Crashed Asserts (Fixed)

| File | Line | Assert | Root Cause |
|------|------|--------|------------|
| `src/chain.cpp` | 112 | `assert(pindexWalk->pprev)` | `GetAncestor` called on unlinked block (pprev==nullptr, height>0) |
| `src/chain.cpp` | 185 | `assert(pa == pb)` | `LastCommonAncestor` on blocks from different chains |
| `src/wallet/wallet.cpp` | 3530 | `assert(conf->confirmed_block_height >= 0)` | Tx deserialized from DB with height=-1 (see `TxStateInterpretSerialized` in `transaction.h:91`) |
| `src/wallet/wallet.cpp` | 3533 | `assert(conf->conflicting_block_height >= 0)` | Same - deserialized conflicted tx with height=-1 |
| `src/wallet/wallet.cpp` | 2085 | `assert(wtx.GetHash() == wtxid)` | Hash mismatch in mapWallet iteration |

## Highest Risk (same pattern as already-crashed)

| File | Line | Assert | Risk |
|------|------|--------|------|
| `src/wallet/wallet.h` | 1002 | `assert(m_last_block_processed_height >= 0)` | Same negative-height pattern. Could fail if block height is corrupted or uninitialized during startup. |
| `src/wallet/wallet.h` | 1008 | `assert(m_last_block_processed_height >= 0)` | Same as above. |
| `src/wallet/wallet.cpp` | 3552 | `assert(chain_depth >= 0)` | Depth = `GetLastBlockHeight() - confirmed_height + 1`. If confirmed_height > last_block_height (e.g. reorg), chain_depth goes negative. Comment says "coinbase tx should not be conflicted" but PoS coinstakes can reorg. |

## Moderate Risk (state assumptions)

| File | Line | Assert | Risk |
|------|------|--------|------|
| `src/wallet/wallet.cpp` | 1351-1352 | `assert(!wtx.isConfirmed())`, `assert(!wtx.InMempool())` | Called during `SyncTransaction`. If a tx is reorged and state is inconsistent, these could fire. |
| `src/wallet/wallet.cpp` | 1124-1125 | `assert(TxStateSerializedIndex(wtx.m_state) == TxStateSerializedIndex(state))` | Assert that serialized index matches between cached and new state. Could fail on corrupted wallet data. |
| `src/wallet/wallet.cpp` | 1343 | `assert(it != mapWallet.end())` | Transaction lookup failure. |
| `src/wallet/wallet.cpp` | 1419 | `assert(it != mapWallet.end())` | Same. |

## Indexer Asserts

| File | Line | Assert | Risk |
|------|------|--------|------|
| `src/index/base.cpp` | 40-41 | `assert(found)`, `assert(!locator.IsNull())` | During `BaseIndex::Init`. Could fail if block locator is corrupted or missing. |
| `src/index/base.cpp` | 260-261 | `assert(current_tip == m_best_block_index)`, `assert(GetAncestor)` | During index rewind. Could fail if chain state is inconsistent (issue #22 scenario). |
| `src/index/base.cpp` | 451 | `assert(!m_chainstate->m_blockman.IsPruneMode() \|\| AllowPrune())` | **Dead code** — commented out entirely since Blackcoin doesn't allow pruning. |

## Low Risk (internal consistency, unlikely in practice)

These are internal invariant checks that would indicate a programming bug rather than runtime data corruption:
- `wtx.GetHash() == wtxid` (now fixed)
- `src/wallet/wallet.cpp:1806` `assert(m_wallet_flags == 0)` — wallet flags must be zero before loading
- `src/wallet/wallet.cpp:1804` — flag consistency check
- `src/wallet/spend.cpp:951-959` — locktime/nSequence assertions
- `src/chain.cpp:95` `assert(height > nHeight || height < 0)` — boundary check in GetAncestor (trivially safe — just returns nullptr)

## Notes

- The `TxStateInterpretSerialized` function in `transaction.h:84-96` is the root cause of the -1 height values. It creates `TxStateConfirmed` and `TxStateBlockConflicted` with `height=-1` as a sentinel for unknown height when deserializing from the wallet database.
- The `!wtx.isConfirmed()` guard added to `AbandonOrphanedCoinstakes` prevents -1-height confirmed txs from being erroneously abandoned after the `GetTxDepthInMainChain` assert was relaxed to `return 0` for negative heights.
- All asserts above are compiled out in release builds (`NDEBUG`), causing silent UB instead of crashes. The debug builds crash cleanly with a message.
