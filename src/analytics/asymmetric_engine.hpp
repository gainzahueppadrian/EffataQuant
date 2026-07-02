#pragma once

#include <cmath>
#include <string>
#include "../pricing/aad_greeks.hpp"

#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::analytics {

struct GammaScalpingMetrics {
    double gamma_scalping_pnl;
    double theta_cost;
    double net_edge;
    double edge_ratio;
};

struct VolatilityConvexityMetrics {
    double base_vega;
    double vomma;
    double vega_after_shock;
    double convexity_benefit;
    std::string exposure_type;
};

struct CharmHedgeUnwindMetrics {
    double daily_charm;
    double delta_change_xd;
    double mm_hedge_flow_per_contract;
    std::string flow_direction;
    std::string intensity;
};

class AsymmetricStrategyEngine {
public:
    ALWAYS_INLINE static GammaScalpingMetrics analyze_gamma_scalping(
        const pricing::AADResult& greeks,
        double spot_price,
        double realized_vol,
        double implied_vol)
    {
        GammaScalpingMetrics metrics{};
        double dt = 1.0 / 365.0; // Daily timeframe

        metrics.gamma_scalping_pnl = 0.5 * greeks.gamma * std::pow(spot_price, 2) *
                                     (std::pow(realized_vol, 2) - std::pow(implied_vol, 2)) * dt;

        metrics.theta_cost = greeks.theta; // Theta is already daily in our C++ model

        metrics.net_edge = metrics.gamma_scalping_pnl + metrics.theta_cost;

        metrics.edge_ratio = (metrics.theta_cost != 0.0) ?
                             std::abs(metrics.gamma_scalping_pnl / metrics.theta_cost) : 0.0;

        return metrics;
    }

    ALWAYS_INLINE static VolatilityConvexityMetrics analyze_volatility_convexity(
        const pricing::AADResult& greeks,
        double vol_shock = 0.05)
    {
        VolatilityConvexityMetrics metrics{};
        metrics.base_vega = greeks.vega;
        metrics.vomma = greeks.vomma;

        metrics.vega_after_shock = metrics.base_vega + metrics.vomma * vol_shock;
        metrics.convexity_benefit = metrics.vega_after_shock - metrics.base_vega;

        if (metrics.vomma > 0) {
            metrics.exposure_type = "Long Vol Convexity";
        } else {
            metrics.exposure_type = "Short Vol Convexity";
        }

        return metrics;
    }

    ALWAYS_INLINE static CharmHedgeUnwindMetrics analyze_charm_unwind(
        const pricing::AADResult& greeks,
        double days_forward = 1.0)
    {
        CharmHedgeUnwindMetrics metrics{};

        // Convert annual charm to daily charm
        metrics.daily_charm = greeks.charm / 365.0;
        metrics.delta_change_xd = metrics.daily_charm * days_forward;

        metrics.mm_hedge_flow_per_contract = metrics.delta_change_xd * 100.0;

        if (metrics.mm_hedge_flow_per_contract > 0) {
            metrics.flow_direction = "MM BUYING";
        } else {
            metrics.flow_direction = "MM SELLING";
        }

        double abs_flow = std::abs(metrics.mm_hedge_flow_per_contract);
        if (abs_flow > 20.0) {
            metrics.intensity = "HIGH";
        } else if (abs_flow > 10.0) {
            metrics.intensity = "MODERATE";
        } else {
            metrics.intensity = "LOW";
        }

        return metrics;
    }
};

} // namespace berkshire::analytics
