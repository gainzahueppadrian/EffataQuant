#pragma once

#include <cmath>
#include <algorithm>
#include "../pricing/aad_greeks.hpp"

#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::risk {

/**
 * @brief Computes the Greeks-Enhanced Kelly fraction for optimal asymmetric compounding
 * Formulas based on Advanced Options Econophysics Strategic Analysis.
 */
class GreeksKelly {
public:
    /**
     * @brief Computes base Kelly fraction f*
     * @param win_rate p (e.g. 0.60 for 60%)
     * @param win_loss_ratio b (avg_win / avg_loss)
     */
    ALWAYS_INLINE static double compute_base_kelly(double win_rate, double win_loss_ratio) {
        if (win_loss_ratio <= 0.0) return 0.0;
        double q = 1.0 - win_rate;
        double f_star = (win_rate * win_loss_ratio - q) / win_loss_ratio;
        return std::max(0.0, f_star);
    }

    /**
     * @brief Computes the Greeks-Enhanced Kelly Fraction
     * @param base_kelly f*
     * @param greeks The AADResult containing the current portfolio or leg Greeks
     * @param trend The market trend signal (1 for bullish, -1 for bearish, 0 for neutral)
     * @param forecast_iv The predicted IV from the Unscented Kalman Filter
     */
    ALWAYS_INLINE static double compute_enhanced_kelly(
        double base_kelly,
        const pricing::AADResult& greeks,
        double trend,
        double forecast_iv)
    {
        // F_dir = 0.5 + 0.5 * sign(Delta * trend)
        double delta_align = greeks.delta * trend;
        double sign_align = (delta_align > 0) ? 1.0 : ((delta_align < 0) ? -1.0 : 0.0);
        double F_dir = 0.5 + 0.5 * sign_align;

        // F_vol = 1 + Vega * (forecast_IV - 0.2) * 10
        double F_vol = 1.0 + greeks.vega * (forecast_iv - 0.20) * 10.0;

        // F_conv = 1 + Gamma * 100 + |Vanna| * 10
        double F_conv = 1.0 + greeks.gamma * 100.0 + std::abs(greeks.vanna) * 10.0;

        // F_time = max(0.1, 1 + Theta * 100)
        double F_time = std::max(0.1, 1.0 + greeks.theta * 100.0);

        // F_vomma = 1 + Vomma * 5
        double F_vomma = 1.0 + greeks.vomma * 5.0;

        // Ensure factors do not go violently negative or infinite
        F_vol = std::max(0.1, F_vol);
        F_conv = std::max(0.1, F_conv);
        F_vomma = std::max(0.1, F_vomma);

        double f_adj = base_kelly * F_dir * F_vol * F_conv * F_time * F_vomma;

        // Half-Kelly is standard for institutional risk
        return std::min(1.0, f_adj * 0.5);
    }
};

} // namespace berkshire::risk
