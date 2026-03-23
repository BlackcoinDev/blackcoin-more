// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/blockmanager_args.h>

#include <common/args.h>
#include <node/blockstorage.h>
#include <tinyformat.h>
#include <util/result.h>
#include <util/translation.h>
#include <validation.h>

#include <cstdint>

namespace node {
util::Result<void> ApplyArgsManOptions(const ArgsManager& args, BlockManager::Options& opts)
{
    // Blackcoin: Block pruning is completely removed
    // Staking requires access to the complete blockchain history for kernel validation
    opts.prune_target = 0; // Always disable pruning

    return {};
}
} // namespace node
