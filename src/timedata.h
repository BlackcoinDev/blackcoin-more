// Copyright (c) 2014-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TIMEDATA_H
#define BITCOIN_TIMEDATA_H

#include "util/time.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

// Blackcoin: Timejacking protection. NTP sync is mandatory for staking security.
static const int64_t DEFAULT_MAX_TIME_ADJUSTMENT = 0;

class CNetAddr;

/**
 * Median filter over a stream of values.
 * Returns the median of the last N numbers
 */
template <typename T>
class CMedianFilter
{
private:
    std::vector<T> vValues;
    std::vector<T> vSorted;
    unsigned int nSize;

public:
    CMedianFilter(unsigned int _size, T initial_value) : nSize(_size)
    {
        vValues.reserve(_size);
        vValues.push_back(initial_value);
        vSorted = vValues;
    }

    void input(T value)
    {
        if (vValues.size() == nSize) {
            vValues.erase(vValues.begin());
        }
        vValues.push_back(value);

        vSorted.resize(vValues.size());
        std::copy(vValues.begin(), vValues.end(), vSorted.begin());
        std::sort(vSorted.begin(), vSorted.end());
    }

    T median() const
    {
        int vSortedSize = vSorted.size();
        assert(vSortedSize > 0);
        if (vSortedSize & 1) // Odd number of elements
        {
            return vSorted[vSortedSize / 2];
        } else // Even number of elements
        {
            return (vSorted[vSortedSize / 2 - 1] + vSorted[vSortedSize / 2]) / 2;
        }
    }

    int size() const
    {
        return vValues.size();
    }

    std::vector<T> sorted() const
    {
        return vSorted;
    }
};

/** Functions to keep track of adjusted P2P time */
int64_t GetTimeOffset();

/**
 * CRITICAL: Blackcoin More - Required for PoS kernel validation
 *
 * This function returns the adjusted system time using a median filter
 * over peer-reported timestamps. It is DIFFERENT from GetTime() which
 * returns raw system time.
 *
 * NEVER remove this function during upgrade - Bitcoin 28.x+ removed it
 * but Blackcoin More requires it for PoS stake kernel calculations.
 *
 * Used in:
 *   - src/consensus/tx_verify.cpp (transaction time validation)
 *   - src/validation.cpp (block time validation)
 *   - src/pos.cpp (stake kernel hash calculation)
 *   - src/net_processing.cpp (peer time filtering)
 */
NodeClock::time_point GetAdjustedTime();
void AddTimeData(const CNetAddr& ip, int64_t nTime);

/**
 * Reset the internal state of GetTimeOffset() and AddTimeData().
 */
void TestOnlyResetTimeData();

/**
 * CRITICAL: Blackcoin More - Required for PoS kernel validation
 *
 * Returns GetAdjustedTime() as epoch seconds (int64_t).
 * This is the legacy integer-based interface used throughout
 * the codebase for transaction and block time operations.
 *
 * NEVER remove this function during upgrade.
 * Equivalent to: TicksSinceEpoch<std::chrono::seconds>(GetAdjustedTime())
 */
int64_t GetAdjustedTimeSeconds();

/**
 * CRITICAL: Blackcoin More - Compile-time verification
 *
 * This static_assert ensures GetAdjustedTime() exists.
 * If Bitcoin Core removes this function in a future upgrade,
 * this file will fail to compile, protecting PoS functionality.
 */
static_assert(std::is_invocable_v<decltype(GetAdjustedTime)>,
              "ERROR: GetAdjustedTime() has been removed! This is CRITICAL for PoS validation. "
              "Blackcoin More MUST preserve this function. See UPGRADE.md Section 10.");

#endif // BITCOIN_TIMEDATA_H
