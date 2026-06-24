#pragma once

#include <iostream>
#include <string>

#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::risk {

enum class StrategyType {
    // 1. Core / Foundation
    CashSecuredPut, CoveredCall, TheWheel, PMCC, LEAPS,

    // 2. Vertical Spreads
    BullPutSpread, BearPutSpread, BullCallSpread, BearCallSpread,
    CallDebitSpread, PutDebitSpread, CallCreditSpread, PutCreditSpread, VerticalSpread,

    // 3. Calendar & Diagonal Spreads (Time & Volatility)
    CalendarSpread, CalendarSpreadBought, CalendarSpreadSold,
    LongCalendarPutSpread, DoubleCalendarSpread, DiagonalCallSpread,
    DiagonalPutSpread, DoubleDiagonalSpread,

    // 4. Advanced / Complex Spreads
    IronCondor, IronButterfly, IronBrokenWingButterfly, IronStrangles,
    IronStraddles, InverseIronButterfly, InverseIronStrangles, InverseIronStraddles,
    JadeLizard, ReverseIronCondor, LongButterfly, ShortButterfly, ButterflyWithPuts,
    ProtectivePut, Zebra,

    // 5. Tunnels & Others
    TunnelBullish, TunnelBearish, TunnelDiagonalLeaps, SyntheticDividend,

    // ILLEGAL NAKED OPTIONS (CNMV / MAR / Warren Buffett Rule)
    NakedCall, NakedPut, NakedStrangle, NakedStraddle
};

struct Portfolio {
    double net_liquidation_value;
    double available_margin;

    ALWAYS_INLINE bool has_collateral(double notional_exposure) const {
        return available_margin >= notional_exposure;
    }
};

/**
 * @brief Warren Buffett Compliance Guard
 */
class ComplianceGuard {
public:
    ALWAYS_INLINE static bool is_order_safe(
        StrategyType strat,
        double max_potential_loss,
        double expected_shortfall,
        const Portfolio& portfolio) {

        if (strat == StrategyType::NakedCall || strat == StrategyType::NakedPut ||
            strat == StrategyType::NakedStrangle || strat == StrategyType::NakedStraddle) {
            std::cerr << "[RISK GUARD] REJECTED: Naked options violate infinite risk policy (CNMV/MAR Compliance)." << std::endl;
            return false;
        }

        double cvar_limit = portfolio.net_liquidation_value * 0.02;
        if (expected_shortfall > cvar_limit) {
            return false;
        }

        if (!portfolio.has_collateral(max_potential_loss)) {
            return false;
        }

        return true;
    }
};

} // namespace berkshire::risk
