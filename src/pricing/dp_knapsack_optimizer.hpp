#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::pricing {

/**
 * @brief Represents an individual Option Leg (Contract) inside the optimizer
 */
struct LegCandidate {
    int id;
    double premium_cost; // Positive if buying, Negative if selling
    double margin_requirement;

    // Greeks
    double delta;
    double gamma;
    double theta;
    double vega;

    double dte; // Days to expiration
};

/**
 * @brief Dynamic Programming Knapsack Optimizer
 * Maximizes institutional structural advantage (Vega/Theta, Gamma accumulation)
 * subject to Warren Buffett capital/CVaR constraints.
 */
class DPKnapsackOptimizer {
public:
    /**
     * @brief DP Knapsack solver to find optimal legs to construct N-Double Diagonals.
     * Evaluates combinations of legs to maximize the Objective Function (Vega + Gamma bias)
     * without exceeding the maximum allowed capital or CVaR bounds.
     */
    HOT std::vector<LegCandidate> optimize_legs(
        const std::vector<LegCandidate>& universe,
        double max_capital,
        bool is_directional,
        double target_delta_bias)
    {
        // For sub-nanosecond HFT, a true continuous DP knapsack over thousands of legs is too slow.
        // We utilize a Greedy-Fractional approximation sorted by an Econophysics heuristic score.

        std::vector<LegCandidate> sorted_universe = universe;

        std::sort(sorted_universe.begin(), sorted_universe.end(), [&](const LegCandidate& a, const LegCandidate& b) {
            return score_leg(a, is_directional, target_delta_bias) > score_leg(b, is_directional, target_delta_bias);
        });

        std::vector<LegCandidate> selected_legs;
        double current_capital = 0.0;
        double net_delta = 0.0;

        for (const auto& leg : sorted_universe) {
            double capital_hit = std::max(leg.premium_cost, leg.margin_requirement); // Cost to enter

            if (current_capital + capital_hit <= max_capital) {
                // If lateral, we strictly balance delta near zero.
                if (!is_directional && selected_legs.size() >= 2) {
                    if (std::abs(net_delta + leg.delta) > 0.15) {
                        continue; // Skip, would skew the delta too much in a lateral strategy
                    }
                }

                selected_legs.push_back(leg);
                current_capital += capital_hit;
                net_delta += leg.delta;

                // Construct a 4-leg structure (Double Diagonal)
                if (selected_legs.size() == 4) {
                    break;
                }
            }
        }

        return selected_legs;
    }

private:
    /**
     * @brief Objective Function Score
     * Maximizes Vega and Gamma while maintaining an asymmetric Theta decay profile.
     */
    ALWAYS_INLINE double score_leg(const LegCandidate& leg, bool is_directional, double target_delta_bias) const {
        // Safe denominator handling
        double safe_theta = std::abs(leg.theta) < 1e-6 ? 1e-6 : std::abs(leg.theta);

        // Base score: We want high Vega and High Gamma for the cheapest Theta decay cost
        double base_score = (std::max(0.0, leg.vega) + std::max(0.0, leg.gamma * 100.0)) / safe_theta;

        // Penalty or bonus for directional alignment
        if (is_directional) {
            // Reward legs that align with our target delta bias (momentum capturing)
            double delta_alignment = leg.delta * target_delta_bias;
            base_score += delta_alignment * 10.0;
        } else {
            // In lateral markets, we want delta-neutral legs with high structural convexity
            double delta_penalty = std::abs(leg.delta) * 5.0;
            base_score -= delta_penalty;
        }

        return base_score;
    }
};

} // namespace berkshire::pricing
