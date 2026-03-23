// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <policy/feerate.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <txmempool.h>
#include <univalue.h>
#include <univalue.h>
#include <validationinterface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace node {
struct NodeContext;
}

using node::NodeContext;

static RPCHelpMan estimatesmartfee()
{
    return RPCHelpMan{"estimatesmartfee",
        "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
        "confirmation within conf_target blocks if possible and return the number of blocks\n"
        "for which the estimate is valid.\n"
        "Blackcoin: Returns constant fee rate as fee estimation is disabled.\n",
        {
            {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO, "Confirmation target in blocks (1 - 1008)"},
            {"estimate_mode", RPCArg::Type::STR, RPCArg::Default{"conservative"}, "The fee estimate mode (ignored in Blackcoin)."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "feerate", /*optional=*/true, "estimate fee rate in " + CURRENCY_UNIT + "/kvB"},
                {RPCResult::Type::ARR, "errors", /*optional=*/true, "Errors encountered during processing (if there are any)",
                    {
                        {RPCResult::Type::STR, "", "error"},
                    }},
                {RPCResult::Type::NUM, "blocks", "block number where estimate was found"},
        }},
        RPCExamples{
            HelpExampleCli("estimatesmartfee", "6") +
            HelpExampleRpc("estimatesmartfee", "6")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            UniValue result(UniValue::VOBJ);
            // Blackcoin: static fee 0.00100000 BLK/kvB
            result.pushKV("feerate", ValueFromAmount(100000));
            result.pushKV("blocks", 2);
            return result;
        },
    };
}

static RPCHelpMan estimaterawfee()
{
    return RPCHelpMan{"estimaterawfee",
        "\nWARNING: This interface is unstable and may disappear or change!\n"
        "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
        "confirmation within conf_target blocks if possible.\n"
        "Blackcoin: Fee estimation is disabled.\n",
        {
            {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO, "Confirmation target in blocks (1 - 1008)"},
            {"threshold", RPCArg::Type::NUM, RPCArg::Default{0.95}, "The proportion of transactions in a given feerate range that must have been\n"
            "confirmed within conf_target to consider those feerates as high enough."},
        },
        RPCResult{
             RPCResult::Type::OBJ, "", "",
             {
                 {RPCResult::Type::STR, "error", "Error message"},
             }
        },
        RPCExamples{
            HelpExampleCli("estimaterawfee", "6 0.9")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Fee estimation is disabled in Blackcoin");
        },
    };
}

void RegisterFeeRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"util", estimatesmartfee},
        {"util", estimaterawfee},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
