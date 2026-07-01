#pragma once
#include <vector>
#include <optional>
#include <cmath>
#include <algorithm>
#include <limits>
#include <string>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::pricing {

enum class MarketRegime {
    LATERAL_LOW_VOL,
    LATERAL_HIGH_VOL,
    DIRECTIONAL_BULL,
    DIRECTIONAL_BEAR,
    CRISIS
};

struct OptionLeg {
    std::string symbol;
    double strike;
    int expiration_dte;
    bool is_call;
    double price;
    double delta;
    double gamma;
    double theta;
    double vega;
    double iv;
    double cvar; // Precomputed CVaR impact
};

struct KnapsackConfig {
    MarketRegime regime;
    double target_portfolio_delta;
    double max_portfolio_cvar;
    int required_legs;
};

struct OptimizedStructure {
    std::vector<OptionLeg> selected_legs;
    double total_score;
    double total_delta;
    double total_vega;
    double total_gamma;
    double total_theta;
    double total_cvar;
};

class DPKnapsackOptimizer {
public:
    explicit DPKnapsackOptimizer() {}

    HOT std::optional<OptimizedStructure> solve(const KnapsackConfig& config, const std::vector<OptionLeg>& available_legs) {
        if (available_legs.empty() || config.required_legs <= 0) {
            return std::nullopt;
        }

        const int W = 1000;
        double max_cvar = config.max_portfolio_cvar;
        double cvar_step = max_cvar / W;

        std::vector<double> dp(W + 1, -std::numeric_limits<double>::infinity());
        std::vector<int> parent_leg(W + 1, -1);
        std::vector<int> parent_state(W + 1, -1);

        dp[0] = 0.0;
        std::vector<int> leg_count(W + 1, 0);

        for (size_t i = 0; i < available_legs.size(); ++i) {
            const auto& leg = available_legs[i];
            int w_leg = static_cast<int>(std::ceil(leg.cvar / cvar_step));

            if (w_leg > W || w_leg <= 0) continue;

            double leg_score = calculate_leg_score(leg, config);

            for (int w = W; w >= w_leg; --w) {
                int prev_w = w - w_leg;
                int prev_legs = leg_count[prev_w];

                if (dp[prev_w] != -std::numeric_limits<double>::infinity() &&
                    (prev_legs + 1) <= config.required_legs) {

                    double new_score = dp[prev_w] + leg_score;

                    if (new_score > dp[w] || (new_score == dp[w] && (prev_legs + 1) > leg_count[w])) {
                        dp[w] = new_score;
                        parent_leg[w] = static_cast<int>(i);
                        parent_state[w] = prev_w;
                        leg_count[w] = prev_legs + 1;
                    }
                }
            }
        }

        double best_score = -std::numeric_limits<double>::infinity();
        int best_w = -1;

        for (int w = 0; w <= W; ++w) {
            if (leg_count[w] == config.required_legs && dp[w] > best_score) {
                best_score = dp[w];
                best_w = w;
            }
        }

        if (best_w == -1) {
            return std::nullopt;
        }

        OptimizedStructure result;
        int current_w = best_w;
        while (current_w > 0 && parent_leg[current_w] != -1) {
            int leg_idx = parent_leg[current_w];
            result.selected_legs.push_back(available_legs[leg_idx]);
            current_w = parent_state[current_w];
        }

        calculate_structure_metrics(result, config);

        if (std::abs(result.total_delta - config.target_portfolio_delta) > 0.15) {
            return std::nullopt;
        }

        return result;
    }

private:
    ALWAYS_INLINE double calculate_leg_score(const OptionLeg& leg, const KnapsackConfig& config) const {
        double base_score = (leg.vega + leg.gamma) / std::max(std::abs(leg.theta), 0.001);

        if (config.regime == MarketRegime::LATERAL_LOW_VOL || config.regime == MarketRegime::LATERAL_HIGH_VOL) {
            base_score -= 5.0 * std::abs(leg.delta);
        } else if (config.regime == MarketRegime::DIRECTIONAL_BULL) {
            if (leg.delta > 0) base_score += 3.0 * leg.delta;
            else base_score -= 2.0 * std::abs(leg.delta);
        } else if (config.regime == MarketRegime::DIRECTIONAL_BEAR) {
            if (leg.delta < 0) base_score += 3.0 * std::abs(leg.delta);
            else base_score -= 2.0 * leg.delta;
        }

        if (leg.vega < 0) base_score -= 10.0;
        if (leg.gamma < 0) base_score -= 5.0;

        return base_score;
    }

    void calculate_structure_metrics(OptimizedStructure& result, const KnapsackConfig& config) const {
        result.total_score = 0.0;
        result.total_delta = 0.0;
        result.total_vega = 0.0;
        result.total_gamma = 0.0;
        result.total_theta = 0.0;
        result.total_cvar = 0.0;

        for (const auto& leg : result.selected_legs) {
            result.total_score += calculate_leg_score(leg, config);
            result.total_delta += leg.delta;
            result.total_vega += leg.vega;
            result.total_gamma += leg.gamma;
            result.total_theta += leg.theta;
            result.total_cvar += leg.cvar;
        }
    }
};

} // namespace berkshire::pricing
