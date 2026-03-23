// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2011-2013 The PPCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// UPGRADE NOTE: This file contains CRITICAL PoS functions that MUST be preserved
// during Bitcoin 26.x → 30.x upgrade. See UPGRADE.md Section 2.2.
//
// Files to preserve: src/pos.cpp, src/pos.h
// Functions that depend on: nStakeModifier (CBlockIndex), GetAdjustedTime()
//
// Bitcoin 30.x does NOT have these functions - they are Blackcoin-specific.

#ifndef BLACKCOIN_POS_H
#define BLACKCOIN_POS_H

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <hash.h>
#include <primitives/transaction.h>
#include <script/sign.h>
#include <stdint.h>
#include <timedata.h>
#include <txdb.h>
#include <validation.h>

using namespace std;

/** UPGRADE NOTE: ComputeStakeModifier is CRITICAL for PoS consensus.
 * Computes the stake modifier from the previous block and kernel hash.
 * This prevents precomputing future stakes.
 *
 * Formula: hash(kernel + previous_stake_modifier)
 * Result stored in CBlockIndex::nStakeModifier (CRITICAL field, not in Bitcoin Core)
 *
 * @param pindexPrev Previous block index (provides nStakeModifier)
 * @param kernel Hash of the kernel transaction output being staked
 * @return New stake modifier to be stored in this block's CBlockIndex
 */
uint256 ComputeStakeModifier(const CBlockIndex* pindexPrev, const uint256& kernel);

struct CStakeCache {
    CStakeCache(uint32_t blockFromTime_, CAmount amount_) : blockFromTime(blockFromTime_), amount(amount_)
    {
    }
    uint32_t blockFromTime;
    CAmount amount;
};

// Check whether the coinstake timestamp meets protocol
// UPGRADE NOTE: Timestamp validation uses GetAdjustedTime() (not GetTime())
// GetAdjustedTime() is REMOVED in Bitcoin 28.x+ - MUST preserve in Blackcoin
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);
bool CheckStakeBlockTimestamp(int64_t nTimeBlock);

// Check if the given UTXO can stake at the given time
// UPGRADE NOTE: Uses GetAdjustedTime() for timestamp - CRITICAL to preserve
bool CheckKernel(CBlockIndex* pindexPrev, unsigned int nBits, uint32_t nTime, const COutPoint& prevout, CCoinsViewCache& view);
bool CheckKernel(CBlockIndex* pindexPrev, unsigned int nBits, uint32_t nTime, const COutPoint& prevout, CCoinsViewCache& view, const std::map<COutPoint, CStakeCache>& cache);

/** UPGRADE NOTE: CheckStakeKernelHash is THE MOST CRITICAL PoS function.
 * Validates that a stake proof meets the target difficulty.
 *
 * CRITICAL DEPENDENCIES (MUST preserve during upgrade):
 * - CBlockIndex::nStakeModifier (NOT in Bitcoin Core)
 * - GetAdjustedTime() (REMOVED in Bitcoin 28.x+)
 *
 * Formula: hash(nStakeModifier + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime) < bnTarget * nWeight
 *
 * This function does NOT exist in Bitcoin Core - it is Blackcoin-specific.
 * Used to verify that a stake transaction actually meets the difficulty target.
 *
 * @param pindexPrev Previous block (provides nStakeModifier)
 * @param nBits Stake difficulty target
 * @param blockFromTime Timestamp of the kernel transaction
 * @param prevoutValue Value of the UTXO being staked
 * @param prevout The UTXO being staked
 * @param nTimeTx Current timestamp (must use GetAdjustedTimeSeconds())
 * @param fPrintProofOfStake If true, log detailed proof information
 * @return true if stake proof is valid
 */
bool CheckStakeKernelHash(const CBlockIndex* pindexPrev, unsigned int nBits, uint32_t blockFromTime, CAmount prevoutValue, const COutPoint& prevout, unsigned int nTimeTx, bool fPrintProofOfStake = false);
bool CheckProofOfStake(CBlockIndex* pindexPrev, const CTransaction& tx, unsigned int nBits, BlockValidationState& state, CCoinsViewCache& view, unsigned int nTimeTx);
void CacheKernel(std::map<COutPoint, CStakeCache>& cache, const COutPoint& prevout, CBlockIndex* pindexPrev, CCoinsViewCache& view);
#endif // BLACKCOIN_POS_H
