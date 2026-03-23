// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// UPGRADE NOTE: Blackcoin More PoS miner
// CRITICAL DIFFERENCES FROM BITCOIN CORE:
// - GetAdjustedTimeSeconds(): REQUIRED for PoS block timestamping
// - PoS kernel: Uses nStakeModifier from CBlockIndex (not in Bitcoin 30.x)
// - Static fees: 100,000 sat/kvB - no fee estimation needed
// - RBF: DISABLED - Bitcoin Core's coinbase selection not applicable
// See UPGRADE.md and src/pos.cpp for complete PoS details.

// PoSMiner by Peercoin
// Copyright (c) 2020-2022 The Peercoin developers

// Staking start/stop algos by Qtum
// Copyright (c) 2016-2023 The Qtum developers

#include <node/miner.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <init.h>
#include <logging.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pos.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <timedata.h>
#include <util/exception.h>
#include <util/moneystr.h>
#include <util/thread.h>
#include <util/threadnames.h>
#include <util/time.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>
#include <warnings.h>
#ifdef ENABLE_WALLET
#include <wallet/staking.h>
#endif

#include <algorithm>
#include <thread>
#include <utility>

using wallet::CCoinControl;
using wallet::COutput;
using wallet::CWallet;
using wallet::CWalletTx;
using wallet::ReserveDestination;

namespace node {

int64_t UpdateTime(CBlock* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime{std::max<int64_t>(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTimeSeconds())}; // BLACKCOIN-SPECIFIC: GetAdjustedTimeSeconds() preserved for PoS

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextTargetRequired(pindexPrev, consensusParams, pblock->IsProofOfStake());
    }

    return nNewTime - nOldTime;
}

int64_t GetMaxTransactionTime(CBlock* pblock)
{
    int64_t maxTransactionTime = 0;
    for (std::vector<CTransactionRef>::const_iterator it(pblock->vtx.begin()); it != pblock->vtx.end(); ++it)
        maxTransactionTime = std::max(maxTransactionTime, (int64_t)it->get()->nTime);
    return maxTransactionTime;
}

void RegenerateCommitments(CBlock& block, ChainstateManager& chainman)
{
    CMutableTransaction tx{*block.vtx.at(0)};
    tx.vout.erase(tx.vout.begin() + GetWitnessCommitmentIndex(block));
    block.vtx.at(0) = MakeTransactionRef(tx);

    const CBlockIndex* prev_block = WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock));
    chainman.GenerateCoinbaseCommitment(block, prev_block);

    block.hashMerkleRoot = BlockMerkleRoot(block);
}

static BlockAssembler::Options ClampOptions(BlockAssembler::Options options)
{
    // Limit weight to between 4K and DEFAULT_BLOCK_MAX_WEIGHT for sanity:
    options.nBlockMaxWeight = std::clamp<size_t>(options.nBlockMaxWeight, 4000, DEFAULT_BLOCK_MAX_WEIGHT);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const CTxMemPool* mempool, const Options& options)
    : chainparams{chainstate.m_chainman.GetParams()},
      m_mempool{mempool},
      m_chainstate{chainstate},
      m_options{ClampOptions(options)}
{
}

void ApplyArgsManOptions(const ArgsManager& args, BlockAssembler::Options& options)
{
    // Block resource limits
    options.nBlockMaxWeight = args.GetIntArg("-blockmaxweight", options.nBlockMaxWeight);
    if (const auto blockmintxfee{args.GetArg("-blockmintxfee")}) {
        if (const auto parsed{ParseMoney(*blockmintxfee)}) options.blockMinFeeRate = CFeeRate{*parsed};
    }
}
static BlockAssembler::Options ConfiguredOptions()
{
    BlockAssembler::Options options;
    ApplyArgsManOptions(gArgs, options);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const CTxMemPool* mempool)
    : BlockAssembler(chainstate, mempool, ConfiguredOptions()) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool* pfPoSCancel, int64_t* pFees, CTxDestination destination)
{
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if (!pblocktemplate.get()) {
        return nullptr;
    }
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1);       // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK(::cs_main);
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = m_chainstate.m_chainman.m_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = GetAdjustedTimeSeconds(); // BLACKCOIN-SPECIFIC: GetAdjustedTimeSeconds() preserved for PoS
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization).
    // Note that the mempool would accept transactions with witness data before
    // the deployment is active, but we would only ever mine blocks after activation
    // unless there is a massive block reorganization with the witness softfork
    // not activated.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = DeploymentActiveAfter(pindexPrev, m_chainstate.m_chainman, Consensus::DEPLOYMENT_SEGWIT);

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    if (m_mempool) {
        LOCK(m_mempool->cs);
        addPackageTxs(*m_mempool, nPackagesSelected, nDescendantsUpdated, pblock->nTime);
    }

    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);

    // Proof-of-work block
    if (!pwallet) {
        pblock->nBits = GetNextTargetRequired(pindexPrev, chainparams.GetConsensus(), false);
        coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
        coinbaseTx.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    }

    // Proof-of-stake block
#ifdef ENABLE_WALLET
    if (pwallet) {
        // attempt to find a coinstake
        *pfPoSCancel = true;
        pblock->nBits = GetNextTargetRequired(pindexPrev, chainparams.GetConsensus(), true);
        CMutableTransaction txCoinStake;
        txCoinStake.nTime &= ~chainparams.GetConsensus().nStakeTimestampMask;

        // BLACKCOIN-SPECIFIC: Check if safety bump was pre-calculated using network time (updatedBlockTip)
        // If so, use the pre-calculated sleep duration directly — avoids redundant MTP/window recalculation.
        //
        // IMPORTANT: The pre-calculated nNextWindow timestamp is NOT used here because:
        // 1. By the time we wake up, MTP may have advanced beyond the pre-calculated window
        // 2. CreateNewBlock() assigns a fresh timestamp anyway (line 226 masks current time)
        // 3. Using a stale future window would cause kernel validation to fail
        //
        // The sleep duration (m_safety_bump_sleep_ms) is still valid — it's a relative wait time.
        int64_t nSafetyBumpSleepMs = 0;
        int64_t precalcSleep = pwallet->m_safety_bump_sleep_ms.load();
        if (precalcSleep > 0) {
            // Use pre-calculated sleep duration from updatedBlockTip()
            // Window recalculation will happen naturally via the fallback path after we wake
            nSafetyBumpSleepMs = precalcSleep;
            LogPrint(BCLog::COINSTAKE, "Minter: Using pre-calculated safety bump sleep=%lld ms (from UpdatedBlockTip)\n",
                     nSafetyBumpSleepMs);
        } else {
            // No pre-calculated value — use original calculation
            // BLACKCOIN-SPECIFIC: "Safety Bump" Logic.
            // If the current slot (e.g., :00) is already taken by the previous block (MTP), we are guaranteed to fail.
            // Instead of failing and retrying in a tight loop, proactively bump to the *next* window (e.g., :16).
            if (txCoinStake.nTime <= pindexPrev->GetMedianTimePast()) {
                uint32_t oldTime = txCoinStake.nTime;
                while (txCoinStake.nTime <= pindexPrev->GetMedianTimePast()) {
                    txCoinStake.nTime += (chainparams.GetConsensus().nStakeTimestampMask + 1);
                }

                // Calculate how long to sleep until the bumped window begins
                // We need to wait until real time reaches txCoinStake.nTime
                int64_t now = GetAdjustedTimeSeconds();
                int64_t timeUntilWindow = (txCoinStake.nTime - now) * 1000;
                if (timeUntilWindow > 0) {
                    nSafetyBumpSleepMs = timeUntilWindow;
                    
                    // CRITICAL: Strip artificial MTP inflation using modulo.
                    // If MTP is manipulated +14s into the future, sleepMs becomes 30000.
                    // Modulo 16000 strips the inflation: 30000 % 16000 = 14000.
                    // This preserves the true offset to the next window boundary.
                    if (nSafetyBumpSleepMs > 16000) {
                        LogPrint(BCLog::COINSTAKE, "Minter: Stripping MTP inflation from sleep: %lld ms -> %lld ms\n",
                                 nSafetyBumpSleepMs, nSafetyBumpSleepMs % 16000);
                        nSafetyBumpSleepMs %= 16000;
                        if (nSafetyBumpSleepMs == 0) nSafetyBumpSleepMs = 16000;
                    }
                }

                LogPrint(BCLog::COINSTAKE, "Minter: Safety Bump triggered! Skipped window %d, starting search at %d (Next Window), sleeping %lld ms\n",
                         oldTime, txCoinStake.nTime, nSafetyBumpSleepMs);
            }
        }

        // BLACKCOIN-SPECIFIC: Individual wallet timer for multi-wallet staker.
        // Prevents a frequent staker (smaller wallet) from starving a slower staker (larger wallet).
        // Each wallet maintains its own independent search timer and interval.
        if (pwallet->m_last_coin_stake_search_time == 0) {
            pwallet->m_last_coin_stake_search_time = GetAdjustedTimeSeconds();
            LogPrint(BCLog::COINSTAKE, "Wallet timer initialized: last_search_time=%d\n",
                     pwallet->m_last_coin_stake_search_time);
        }

        int64_t nSearchTime = txCoinStake.nTime; // search to current time

        // BLACKCOIN-SPECIFIC: Complete multi-wallet independence - no shared static timer
        // Each wallet searches independently without competing for time windows
        if (nSearchTime > pwallet->m_last_coin_stake_search_time) {
            if (nSearchTime - pindexPrev->GetMedianTimePast() < 2) {
                LogPrint(BCLog::COINSTAKE, "WARNING: Close MTP collision detected (search: %d, MTP: %d, diff: %d)\n",
                         nSearchTime,
                         pindexPrev->GetMedianTimePast(),
                         nSearchTime - pindexPrev->GetMedianTimePast());
            }

            if (wallet::CreateCoinStake(*pwallet, pblock->nBits, 1, txCoinStake, nFees, destination)) {
                if (txCoinStake.nTime >= pindexPrev->GetMedianTimePast() + 1) {
                    // Make the coinbase tx empty in case of proof of stake
                    coinbaseTx.vout[0].SetEmpty();
                    pblock->nTime = coinbaseTx.nTime = txCoinStake.nTime;
                    pblock->vtx.insert(pblock->vtx.begin() + 1, MakeTransactionRef(CTransaction(txCoinStake)));
                    *pfPoSCancel = false;

                    // BLACKCOIN-SPECIFIC: Log successful kernel creation with statistics
                    // Shows per-wallet independent staking performance
                    pwallet->WalletLogPrintf("COINSTAKE CREATED: found kernel at timestamp %d, hash %s, search time %dms\n",
                              txCoinStake.nTime,
                              txCoinStake.GetHash().GetHex().c_str(),
                              pwallet->m_last_coin_stake_search_interval);

                    // BLACKCOIN-SPECIFIC: Update per-wallet timer for next search cycle
                    // Each wallet maintains independent search state
                    pwallet->m_last_coin_stake_search_interval = nSearchTime - pwallet->m_last_coin_stake_search_time;
                    pwallet->m_last_coin_stake_search_time = nSearchTime;
                    LogPrint(BCLog::COINSTAKE, "Wallet timer updated: interval=%d, last_search_time=%d\n",
                             pwallet->m_last_coin_stake_search_interval, pwallet->m_last_coin_stake_search_time);
                 } else {
                    // BLACKCOIN-SPECIFIC: GHOST BLOCK DIAGNOSTIC LOGGING
                    // IMPORTANT: With the safety bump mechanism, this should NEVER trigger.
                    // Safety bump guarantees txTime > MTP before kernel search starts.
                    // If this logs, it indicates a bug (race condition or timing issue).
                    // Left as diagnostic logging for debugging purposes only.
                    LogPrint(BCLog::COINSTAKE, "GHOST BLOCK DETECTED: Valid kernel dropped due to timestamp validation failure\n");
                    LogPrint(BCLog::COINSTAKE, "  Kernel Timestamp: %d (masked: %d)\n",
                              txCoinStake.nTime,
                              txCoinStake.nTime & ~chainparams.GetConsensus().nStakeTimestampMask);
                    LogPrint(BCLog::COINSTAKE, "  MedianTimePast: %d (MTP+1: %d)\n",
                              pindexPrev->GetMedianTimePast(),
                              pindexPrev->GetMedianTimePast() + 1);
                    LogPrint(BCLog::COINSTAKE, "  Reason: Timestamp %d < MTP+1 (%d)\n",
                              txCoinStake.nTime,
                              pindexPrev->GetMedianTimePast() + 1);
                    LogPrint(BCLog::COINSTAKE, "  Kernel Hash: %s\n",
                              txCoinStake.GetHash().GetHex().c_str());
                    LogPrint(BCLog::COINSTAKE, "  Stake Modifier: %s\n",
                              pindexPrev->nStakeModifier.GetHex().c_str());

                    // Additional diagnostic: Check for 16-second masking collision
                    uint32_t maskedTime = txCoinStake.nTime & ~chainparams.GetConsensus().nStakeTimestampMask;
                    if (maskedTime <= pindexPrev->GetMedianTimePast()) {
                        LogPrint(BCLog::COINSTAKE, "  DIAGNOSTIC: 16-second mask collision detected (masked: %d <= MTP: %d)\n",
                                  maskedTime,
                                  pindexPrev->GetMedianTimePast());
                    }
                }
            }
            // BLACKCOIN-SPECIFIC: Update per-wallet search state even when no kernel found
            // Maintains independent search windows for each wallet to prevent starvation
            pwallet->m_last_coin_stake_search_interval = nSearchTime - pwallet->m_last_coin_stake_search_time;
            pwallet->m_last_coin_stake_search_time = nSearchTime;
        }
        if (*pfPoSCancel) {
            // BLACKCOIN-SPECIFIC: If safety bump sleep is needed, pass it to the caller
            if (nSafetyBumpSleepMs > 0) {
                pwallet->m_safety_bump_sleep_ms = nSafetyBumpSleepMs;
            }
            return nullptr; // peercoin: there is no point to continue if we failed to create coinstake
        }
        pblock->nFlags = CBlockIndex::BLOCK_PROOF_OF_STAKE;
    }
#endif

    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    if (fIncludeWitness)
        pblocktemplate->vchCoinbaseCommitment = m_chainstate.m_chainman.GenerateCoinbaseCommitment(*pblock, pindexPrev);
    pblocktemplate->vTxFees[0] = -nFees;

    LogPrintf("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    if (pFees)
        *pFees = nFees;

    // Fill in header
    pblock->hashPrevBlock = pindexPrev->GetBlockHash();
    pblock->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetMaxTransactionTime(pblock));
    if (!pblock->IsProofOfStake())
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nNonce = 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    BlockValidationState state;
    if (!pblock->IsProofOfStake() && m_options.test_block_validity && !TestBlockValidity(state, chainparams, m_chainstate, *pblock, pindexPrev,
                                                                                         /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
    }
    const auto time_2{SteadyClock::now()};

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start), nPackagesSelected, nDescendantsUpdated,
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end();) {
        // Only test txs not already in the block
        if (inBlock.count((*iit)->GetSharedTx()->GetHash())) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= m_options.nBlockMaxWeight) {
        return false;
    }
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST) {
        return false;
    }
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
// - transaction timestamp limit
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package, uint32_t nTime) const
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, m_lock_time_cutoff)) {
            return false;
        }
        if (!fIncludeWitness && it->GetTx().HasWitness()) {
            return false;
        }
        // peercoin: timestamp limit
        if (it->GetTx().nTime > GetAdjustedTimeSeconds() || (nTime && it->GetTx().nTime > nTime)) { // BLACKCOIN-SPECIFIC: GetAdjustedTimeSeconds() preserved
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblocktemplate->block.vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter->GetSharedTx()->GetHash());

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee rate %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

/** Add descendants of given transactions to mapModifiedTx with ancestor
 * state updated assuming given transactions are inBlock. Returns number
 * of updated descendants. */
static int UpdatePackagesForAdded(const CTxMemPool& mempool,
                                  const CTxMemPool::setEntries& alreadyAdded,
                                  indexed_modified_transaction_set& mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs)
{
    AssertLockHeld(mempool.cs);

    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc)) {
                continue;
            }
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                mit = mapModifiedTx.insert(modEntry).first;
            }
            mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
        }
    }
    return nDescendantsUpdated;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(const CTxMemPool& mempool, int& nPackagesSelected, int& nDescendantsUpdated, uint32_t nTime)
{
    AssertLockHeld(mempool.cs);

    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    std::set<Txid> failedTx;

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        //
        // Skip entries in mapTx that are already in a block or are present
        // in mapModifiedTx (which implies that the mapTx ancestor state is
        // stale due to ancestor inclusion in the block)
        // Also skip transactions that we've already failed to add. This can happen if
        // we consider a transaction in mapModifiedTx and it fails: we can then
        // potentially consider it again while walking mapTx.  It's currently
        // guaranteed to fail again, but as a belt-and-suspenders check we put it in
        // failedTx and avoid re-evaluation, since the re-evaluation would be using
        // cached size/sigops/fee values that are not actually correct.
        /** Return true if given transaction from mapTx has already been evaluated,
         * or if the transaction's cached data in mapTx is incorrect. */
        if (mi != mempool.mapTx.get<ancestor_score>().end()) {
            auto it = mempool.mapTx.project<0>(mi);
            assert(it != mempool.mapTx.end());
            if (mapModifiedTx.count(it) || inBlock.count(it->GetSharedTx()->GetHash()) || failedTx.count(it->GetSharedTx()->GetHash())) {
                ++mi;
                continue;
            }
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter->GetSharedTx()->GetHash()));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (packageFees < m_options.blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter->GetSharedTx()->GetHash());
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                                                                     m_options.nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        auto ancestors{mempool.AssumeCalculateMemPoolAncestors(__func__, *iter, CTxMemPool::Limits::NoLimits(), /*fSearchForParents=*/false)};

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors, nTime)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter->GetSharedTx()->GetHash());
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(mempool, ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce));
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

// Peercoin/Blackcoin
static bool ProcessBlockFound(const CBlock* pblock, ChainstateManager& chainman)
{
    LogPrintf("%s", pblock->ToString());

    // Found a solution
    {
        LOCK(cs_main);
        BlockValidationState state;
        if (!CheckProofOfStake(&chainman.BlockIndex()[pblock->hashPrevBlock], *pblock->vtx[1], pblock->nBits, state, chainman.ActiveChainstate().CoinsTip(), pblock->vtx[1]->nTime ? pblock->vtx[1]->nTime : pblock->nTime))
            return error("ProcessBlockFound(): proof-of-stake checking failed");

        if (pblock->hashPrevBlock != chainman.ActiveChain().Tip()->GetBlockHash())
            return error("ProcessBlockFound(): generated block is stale");
    }

    // Process this block the same as if we had received it from another node
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!chainman.ProcessNewBlock(shared_pblock, true, true, nullptr))
        return error("ProcessBlockFound(): block not accepted");

    return true;
}

#ifdef ENABLE_WALLET
// BLACKCOIN-SPECIFIC: Sleep with wake-on-block-arrival mechanism for safety bump optimization
// Replaces naive UninterruptibleSleep() with condition variable that allows early wake-up
// when new blocks arrive on the active chain, enabling immediate safety bump recalculation
// qtum
// BLACKCOIN-SPECIFIC: Enhanced with proper notification race handling
bool SleepStaker(CWallet* pwallet, uint64_t milliseconds)
{
    // BLACKCOIN-SPECIFIC: Wake early when new block arrives on active chain
    // Uses condition variable wait_until() for efficient interruptible sleep
    // Only wakes on active chain updates (updatedBlockTip() callback), not on forks
    // Thread-safe: unique_lock protects cv_block_mutex during wait operations
    std::unique_lock<std::mutex> lock(pwallet->cv_block_mutex);

    // Calculate absolute deadline for sleep duration
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);

    // BLACKCOIN-SPECIFIC: CRITICAL FIX - Check flag BEFORE waiting
    // This prevents the race condition where a notification arrives between
    // checking IsStakeClosing() and calling wait_until(), which would be lost.
    // The exchange() atomically reads and clears the flag.
    if (pwallet->m_new_block_arrived.exchange(false)) {
        // Block arrived while we were acquiring the lock - return immediately
        return true;
    }

    // Check for block arrival until deadline or shutdown
    while (std::chrono::steady_clock::now() < deadline) {
        // Check shutdown/closing status before waiting
        if (pwallet->IsStakeClosing())
            return false;

        // Wait until deadline OR new block notification arrives
        // wait_until() is more efficient than polling (no CPU cycles in wait state)
        // Returns cv_status::no_timeout if notified, cv_status::timeout if deadline reached
        auto result = pwallet->cv_new_block.wait_until(lock, deadline);

        if (result == std::cv_status::no_timeout) {
            // BLACKCOIN-SPECIFIC: Check the flag to distinguish real notifications from spurious wakeups
            // Spurious wakeups can occur without notify_one() being called.
            // exchange() atomically reads and clears the flag.
            if (pwallet->m_new_block_arrived.exchange(false)) {
                // New block arrived on active chain (via updatedBlockTip() callback)
                // Return true to indicate early wake-up - caller should retry staking immediately
                return true;
            }
            // Spurious wakeup - continue loop (deadline still valid)
        }
    }

    // Deadline reached without new block arrival
    // Check shutdown status one more time before returning
    return !pwallet->IsStakeClosing();
}

// qtum
bool CanStake()
{
    bool canStake = gArgs.GetBoolArg("-staking", DEFAULT_STAKE);

    if (canStake) {
        // Signet is for creating PoW blocks by an authorized signer
        canStake = !Params().GetConsensus().signet_blocks;
    }

    return canStake;
}

// peercoin: sign block
typedef std::vector<unsigned char> valtype;
bool SignBlock(CBlock& block, const CWallet& keystore)
{
    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.IsProofOfStake() ? block.vtx[1]->vout[1] : block.vtx[0]->vout[0];

    if (Solver(txout.scriptPubKey, vSolutions) != TxoutType::PUBKEY)
        return false;

    // Sign
    if (keystore.IsLegacy()) {
        const valtype& vchPubKey = vSolutions[0];
        CKey key;
        if (!keystore.GetLegacyScriptPubKeyMan()->GetKey(CKeyID(Hash160(vchPubKey)), key))
            return false;
        if (key.GetPubKey() != CPubKey(vchPubKey))
            return false;
        return key.Sign(block.GetHash(), block.vchBlockSig, 0);
    } else {
        CTxDestination address;
        CPubKey pubKey(vSolutions[0]);
        address = PKHash(pubKey);
        PKHash* pkhash = std::get_if<PKHash>(&address);
        SigningResult res = keystore.SignBlockHash(block.GetHash(), *pkhash, block.vchBlockSig);
        if (res == SigningResult::OK)
            return true;
        return false;
    }
}

// peercoin
void PoSMiner(CWallet* pwallet)
{
    // Note: Thread name is set by TraceThread wrapper in StakeCoins()
    pwallet->WalletLogPrintf("PoSMiner started for proof-of-stake\n");

    unsigned int nExtraNonce = 0;

    CTxDestination dest;

    // Compute timeout for pos as sqrt(numUTXO)
    unsigned int pos_timio;
    {
        LOCK2(pwallet->cs_wallet, cs_main);
        const std::string label = "Staking Legacy Address";
        pwallet->ForEachAddrBookEntry([&](const CTxDestination& _dest, const std::string& _label, bool _is_change, const std::optional<wallet::AddressPurpose>& _purpose) {
            if (_is_change) return;
            if (_label == label)
                dest = _dest;
        });

        if (std::get_if<CNoDestination>(&dest)) {
            // create mintkey address
            auto op_dest = pwallet->GetNewDestination(OutputType::LEGACY, label);
            if (!op_dest)
                throw std::runtime_error("Error: Keypool ran out, please call keypoolrefill first.");
            dest = *op_dest;
        }

        std::vector<std::pair<const CWalletTx*, unsigned int>> vCoins;
        CCoinControl coincontrol;
        AvailableCoinsForStaking(*pwallet, vCoins, &coincontrol);
        pos_timio = gArgs.GetIntArg("-staketimio", DEFAULT_STAKETIMIO) + 30 * sqrt(vCoins.size());
        pwallet->WalletLogPrintf("Set proof-of-stake timeout: %ums for %u UTXOs\n", pos_timio, vCoins.size());
    }

    try {
        while (true) {
            while (pwallet->IsLocked() || !pwallet->m_enabled_staking || fReindex || pwallet->chain().chainman().m_blockman.m_importing) {
                pwallet->m_last_coin_stake_search_interval = 0;
                if (!SleepStaker(pwallet, 5000))
                    return;
            }

            // Busy-wait for the network to come online so we don't waste time mining
            // on an obsolete chain. In regtest mode we expect to fly solo.
            if (!Params().MineBlocksOnDemand()) {
                while (pwallet->chain().getNodeCount(ConnectionDirection::Both) == 0 || pwallet->chain().isInitialBlockDownload()) {
                    pwallet->m_last_coin_stake_search_interval = 0;
                    if (!SleepStaker(pwallet, 10000))
                        return;
                }
            }

            while (GuessVerificationProgress(Params().TxData(), pwallet->chain().getTip()) < 0.996) {
                pwallet->m_last_coin_stake_search_interval = 0;
                pwallet->WalletLogPrintf("Staker thread sleeps while sync at %f\n", GuessVerificationProgress(Params().TxData(), pwallet->chain().getTip()));
                if (!SleepStaker(pwallet, 10000))
                    return;
            }

            //
            // Create new block
            //
            CBlockIndex* pindexPrev = pwallet->chain().getTip();
            bool fPoSCancel{false};
            int64_t pFees{0};
            CBlock* pblock;
            std::unique_ptr<CBlockTemplate> pblocktemplate;

            {
                LOCK2(pwallet->cs_wallet, cs_main);
                
                // BLACKCOIN-SPECIFIC: Clear stale wake-up flag INSIDE the lock
                // This prevents a race condition where:
                // 1. Validation thread updates the tip (holding cs_main) and sets flag=true
                // 2. We clear flag=false *outside* the lock
                // 3. Validation thread releases cs_main
                // 4. We acquire cs_main and process the NEW tip
                // 5. We sleep, but abort instantly because the flag was true!
                // By clearing it here, we guarantee we process whatever the state was 
                // when we got the lock, and any new blocks *after* we release it will abort us!
                pwallet->m_new_block_arrived.store(false);
                
                try {
                    pblocktemplate = BlockAssembler{pwallet->chain().chainman().ActiveChainstate(), &pwallet->chain().mempool()}.CreateNewBlock(GetScriptForDestination(dest), pwallet, &fPoSCancel, &pFees, dest);
                } catch (const std::runtime_error& e) {
                    pwallet->WalletLogPrintf("PoSMiner runtime error: %s\n", e.what());
                    continue;
                }
            }

            if (!pblocktemplate.get()) {
                if (fPoSCancel == true) {
                    // Check if safety bump wants us to sleep until a specific time
                    int64_t safetyBumpSleep = pwallet->m_safety_bump_sleep_ms.load();
                    int64_t sleepTime = safetyBumpSleep > 0 ? safetyBumpSleep : pos_timio;
                    pwallet->m_safety_bump_sleep_ms = 0;  // Reset for next iteration
                    
                    if (!SleepStaker(pwallet, sleepTime))
                        return;
                    continue;
                }
                pwallet->WalletLogPrintf("Error in PoSMiner: Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                if (!SleepStaker(pwallet, 10000))
                    return;

                return;
            }
            pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

            // peercoin: if proof-of-stake block found then process block
            if (pblock->IsProofOfStake()) {
                {
                    LOCK2(pwallet->cs_wallet, cs_main);
                    if (!SignBlock(*pblock, *pwallet)) {
                        pwallet->WalletLogPrintf("PoSMiner: failed to sign PoS block\n");
                        continue;
                    }
                }
                pwallet->WalletLogPrintf("PoSMiner: proof-of-stake block found %s\n", pblock->GetHash().ToString());
                ProcessBlockFound(pblock, pwallet->chain().chainman());
                // Rest for ~16 seconds after successful block to preserve close quick
                uint64_t stakerRestTime = (16 + GetRand(4)) * 1000;
                if (!SleepStaker(pwallet, stakerRestTime))
                    return;
                continue;  // Skip staketimio sleep after finding a block
            }
            if (!SleepStaker(pwallet, pos_timio))
                return;

            continue;
        }
    } catch (const std::runtime_error& e) {
        pwallet->WalletLogPrintf("PoSMiner: runtime error: %s\n", e.what());
        return;
    }
}

// peercoin: stake miner thread
void static ThreadStakeMiner(CWallet* pwallet)
{
    pwallet->WalletLogPrintf("ThreadStakeMiner started\n");
    while (true) {
        try {
            PoSMiner(pwallet);
            break;
        } catch (std::exception& e) {
            PrintExceptionContinue(&e, "ThreadStakeMiner()");
        } catch (...) {
            PrintExceptionContinue(nullptr, "ThreadStakeMiner()");
        }
    }
    pwallet->WalletLogPrintf("ThreadStakeMiner stopped\n");
}

// qtum
void StakeCoins(bool fStake, CWallet* pwallet, std::unique_ptr<std::vector<std::thread>>& threadStakeMinerGroup)
{
    // If threadStakeMinerGroup is initialized join all threads and clear the vector
    if (threadStakeMinerGroup) {
        for (std::thread& thread : *threadStakeMinerGroup)
            if (thread.joinable()) thread.join();
        threadStakeMinerGroup->clear();
    }

    if (fStake) {
        threadStakeMinerGroup = std::make_unique<std::vector<std::thread>>();
        // Use TraceThread to set thread name before any code runs (fixes [unknown] in logs)
        threadStakeMinerGroup->emplace_back(std::thread(
            &util::TraceThread,
            strprintf("stake-%s", pwallet->GetName()),
            [pwallet] { ThreadStakeMiner(pwallet); }));
    }
}
#endif

} // namespace node
