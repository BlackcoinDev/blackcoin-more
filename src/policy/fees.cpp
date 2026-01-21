// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/fees.h>

static std::set<double> MakeFeeSet(const CFeeRate& min_incremental_fee,
                                   double max_filter_fee_rate,
                                   double fee_filter_spacing)
{
    std::set<double> fee_set;

    const CAmount min_fee_limit{std::max(CAmount(1), min_incremental_fee.GetFeePerK() / 2)};
    fee_set.insert(0);
    for (double bucket_boundary = min_fee_limit;
         bucket_boundary <= max_filter_fee_rate;
         bucket_boundary *= fee_filter_spacing) {

        fee_set.insert(bucket_boundary);
    }

    return fee_set;
}

FeeFilterRounder::FeeFilterRounder(const CFeeRate& minIncrementalFee, FastRandomContext& rng)
    : m_fee_set{MakeFeeSet(minIncrementalFee, MAX_FILTER_FEERATE, FEE_FILTER_SPACING)},
      insecure_rand{rng}
{
}

class TxConfirmStats {};

CAmount FeeFilterRounder::round(CAmount currentMinFee)
{
    AssertLockNotHeld(m_insecure_rand_mutex);
    std::set<double>::iterator it = m_fee_set.lower_bound(currentMinFee);
    if (it == m_fee_set.end() ||
        (it != m_fee_set.begin() &&
         WITH_LOCK(m_insecure_rand_mutex, return insecure_rand.rand32()) % 3 != 0)) {
        --it;
    }
    return static_cast<CAmount>(*it);
}

CBlockPolicyEstimator::CBlockPolicyEstimator(const fs::path& estimation_filepath, const bool read_stale_estimates)
    : m_estimation_filepath(estimation_filepath)
{
}

CBlockPolicyEstimator::~CBlockPolicyEstimator() {}

void CBlockPolicyEstimator::processBlock(const std::vector<RemovedMempoolTransactionInfo>& txs_removed_for_block, unsigned int nBlockHeight) {}

void CBlockPolicyEstimator::processTransaction(const NewMempoolTransactionInfo& tx) {}

bool CBlockPolicyEstimator::removeTx(uint256 hash) { return false; }

CFeeRate CBlockPolicyEstimator::estimateFee(int confTarget) const { return CFeeRate(0); }

CFeeRate CBlockPolicyEstimator::estimateSmartFee(int confTarget, FeeCalculation *feeCalc, bool conservative) const
{
    if (feeCalc) {
        feeCalc->desiredTarget = confTarget;
        feeCalc->returnedTarget = confTarget;
        feeCalc->reason = FeeReason::NONE;
    }
    return CFeeRate(0);
}

CFeeRate CBlockPolicyEstimator::estimateRawFee(int confTarget, double successThreshold, FeeEstimateHorizon horizon, EstimationResult* result) const { return CFeeRate(0); }

bool CBlockPolicyEstimator::Write(AutoFile& fileout) const { return true; }

bool CBlockPolicyEstimator::Read(AutoFile& filein) { return true; }

void CBlockPolicyEstimator::FlushUnconfirmed() {}

unsigned int CBlockPolicyEstimator::HighestTargetTracked(FeeEstimateHorizon horizon) const { return 0; }

void CBlockPolicyEstimator::Flush() {}

void CBlockPolicyEstimator::FlushFeeEstimates() {}

std::chrono::hours CBlockPolicyEstimator::GetFeeEstimatorFileAge() { return std::chrono::hours(0); }

void CBlockPolicyEstimator::TransactionAddedToMempool(const NewMempoolTransactionInfo& tx, uint64_t) {}

void CBlockPolicyEstimator::TransactionRemovedFromMempool(const CTransactionRef& tx, MemPoolRemovalReason, uint64_t) {}

void CBlockPolicyEstimator::MempoolTransactionsRemovedForBlock(const std::vector<RemovedMempoolTransactionInfo>& txs_removed_for_block, unsigned int nBlockHeight) {}
