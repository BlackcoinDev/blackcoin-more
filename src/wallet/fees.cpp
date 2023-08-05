// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <wallet/fees.h>

#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

namespace wallet {
CFeeRate GetRequiredFeeRate(const CWallet& wallet)
{
    return wallet.chain().relayMinFee();
}

CFeeRate GetMinimumFeeRate(const CWallet& wallet, const CCoinControl& coin_control)
{
    /* User control of how to calculate fee uses the following parameter precedence:
       1. coin_control.m_feerate
       2. m_pay_tx_fee (user-set member variable of wallet)
       The first parameter that is set is used.
    */
    CFeeRate feerate_needed;
    if (coin_control.m_feerate) { // 1.
        feerate_needed = *(coin_control.m_feerate);
        // Allow to override automatic min/max check over coin control instance
        if (coin_control.fOverrideFeeRate) return feerate_needed;
    }
    else if (wallet.m_pay_tx_fee != CFeeRate(0)) { // 2. TODO: remove magic value of 0 for wallet member m_pay_tx_fee
        feerate_needed = wallet.m_pay_tx_fee;
    }

    // prevent user from paying a fee below the required fee rate
    CFeeRate required_feerate = GetRequiredFeeRate(wallet);
    if (required_feerate > feerate_needed) {
        feerate_needed = required_feerate;
    }
    return feerate_needed;
}

CFeeRate GetDiscardRate(const CWallet& wallet)
{
    // Don't let discard_rate be greater than longest possible fee estimate if we get a valid fee estimate
    CFeeRate discard_rate = wallet.m_discard_rate;
    // Discard rate must be at least dust relay feerate
    discard_rate = std::max(discard_rate, wallet.chain().relayDustFee());
    return discard_rate;
}
} // namespace wallet
