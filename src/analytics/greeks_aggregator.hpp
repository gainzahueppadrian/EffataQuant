#pragma once

#include <vector>
#include <iostream>
#include "../pricing/aad_greeks.hpp"
#include "../pricing/dp_knapsack_optimizer.hpp" // For OptionLeg

#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::analytics {

struct PortfolioGreeks {
    double GEX; // Gamma Exposure
    double VEX; // Vega Exposure (Vomma weighted)
    double CHEX; // Charm Exposure

    double total_delta;
    double total_theta;
    double total_vanna;

    // Asymmetric Performance Bounds
    double estimated_sharpe;
    double estimated_sortino;
    double estimated_calmar;
};

class GreeksAggregator {
public:
    ALWAYS_INLINE static PortfolioGreeks aggregate_portfolio(const std::vector<pricing::OptionLeg>& positions, double spot_price) {
        PortfolioGreeks agg{};

        for (const auto& pos : positions) {
            // Greeks aggregate linearly
            agg.total_delta += pos.delta;
            agg.total_theta += pos.theta;
            agg.total_vanna += pos.vanna;

            // GEX_Call = Gamma * OI * 100 * Spot
            // Simplified using our internal size multiplier 100 for standard contracts
            double exposure_mult = 100.0 * spot_price;
            double gex_leg = pos.gamma * exposure_mult;
            if (!pos.is_call) gex_leg *= -1.0;
            agg.GEX += gex_leg;

            // VEX = Vega + Vomma (Convexity of Vega)
            agg.VEX += (pos.vega + pos.vomma * 0.1) * 100.0;

            // CHEX = Charm
            agg.CHEX += pos.charm * 100.0;
        }

        // Extremely simplified heuristics for Monte-Carlo backed risk metrics based on Greek exposures
        agg.estimated_sharpe = (agg.total_theta > 0 && agg.GEX > 0) ? 2.5 : 1.2;
        agg.estimated_sortino = (agg.VEX > 0) ? 3.0 : 1.5;
        agg.estimated_calmar = (agg.total_vanna > 0) ? 2.8 : 1.0;

        return agg;
    }

    static void print_attribution(const PortfolioGreeks& agg) {
        std::cout << "===== GREEKS AGGREGATION & P&L ATTRIBUTION =====" << std::endl;
        std::cout << "GEX (Gamma Muro): " << agg.GEX << std::endl;
        std::cout << "VEX (Vega/Vomma): " << agg.VEX << std::endl;
        std::cout << "CHEX (Charm)    : " << agg.CHEX << std::endl;
        std::cout << "Total Delta     : " << agg.total_delta << std::endl;
        std::cout << "Total Theta     : " << agg.total_theta << std::endl;
        std::cout << "Total Vanna     : " << agg.total_vanna << std::endl;
        std::cout << "Est. Sharpe     : " << agg.estimated_sharpe << std::endl;
        std::cout << "Est. Sortino    : " << agg.estimated_sortino << std::endl;
        std::cout << "Est. Calmar     : " << agg.estimated_calmar << std::endl;
        std::cout << "================================================" << std::endl;
    }
};

} // namespace berkshire::analytics
