// Copyright (c) 2012-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Blackcoin More: RBF is disabled
// This header is kept for test compatibility, but RBF functionality is not used

#ifndef BITCOIN_UTIL_RBF_H
#define BITCOIN_UTIL_RBF_H

#include <cstdint>

// RBF is disabled in Blackcoin More
// Use maximum sequence number (no Replace-By-Fee)
static constexpr uint32_t MAX_BIP125_RBF_SEQUENCE{0xffffffff - 1};

#endif // BITCOIN_UTIL_RBF_H
