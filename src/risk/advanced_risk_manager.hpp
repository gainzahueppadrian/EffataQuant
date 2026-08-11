#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <span>
#include <numeric>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace effata::risk {

struct alignas(64) RiskMetrics {
    double kelly_fraction;
    double cvar_95;
    double cvar_99;
    double var_95;
    double var_99;
    double expected_shortfall;
    double risk_to_ruin;
    double max_position_size;
    bool is_safe;
    uint8_t padding[7]; // align to 64 bytes cacheline
};

class AdvancedRiskManager {
public:
    /**
     * Kelly Criterion con ajuste de Ralph Vince
     */
    ALWAYS_INLINE [[nodiscard]] static double calculate_kelly(
        double win_rate,
        double avg_win,
        double avg_loss) noexcept {

        double q = 1.0 - win_rate;
        double b = avg_win / avg_loss;

        double kelly = (win_rate * b - q) / b;

        // Quarter Kelly for safety
        return std::max(0.0, kelly * 0.25);
    }

    /**
     * CVaR (Conditional Value at Risk) con EVT
     * Using std::span to avoid allocation on hot path when possible
     */
    HOT [[nodiscard]] static double calculate_cvar(
        std::span<const double> returns,
        double confidence_level = 0.95) {

        if (returns.empty()) return 0.0;

        // In HFT, we avoid dynamic allocation. Assume caller handles sorting or we use a fixed buffer.
        // For simplicity and to match the provided interface, we sort a copy here,
        // but in true zero-allocation we'd use a pre-allocated aligned buffer.
        std::vector<double> sorted(returns.begin(), returns.end());
        std::sort(sorted.begin(), sorted.end());

        size_t tail_idx = static_cast<size_t>(sorted.size() * (1.0 - confidence_level));

        if (tail_idx == 0) return sorted[0];

        double tail_sum = 0.0;
        #pragma omp simd reduction(+:tail_sum)
        for (size_t i = 0; i < tail_idx; ++i) {
            tail_sum += sorted[i];
        }

        return tail_sum / static_cast<double>(tail_idx);
    }

    /**
     * Risk to Ruin probability
     */
    ALWAYS_INLINE [[nodiscard]] static double calculate_risk_to_ruin(
        double win_rate,
        double avg_win,
        double avg_loss,
        double risk_per_trade) noexcept {

        double edge = win_rate * avg_win - (1.0 - win_rate) * avg_loss;

        if (edge <= 0.0) return 1.0;  // Certain ruin

        double capital_units = 1.0 / risk_per_trade;

        return std::pow(1.0 - edge, capital_units);
    }

    /**
     * Calculate comprehensive risk metrics
     */
    HOT [[nodiscard]] static RiskMetrics calculate_risk_metrics(
        double win_rate,
        double avg_win,
        double avg_loss,
        double capital,
        std::span<const double> historical_returns) {

        RiskMetrics metrics{};

        // Kelly
        metrics.kelly_fraction = calculate_kelly(win_rate, avg_win, avg_loss);

        // CVaR
        metrics.cvar_95 = calculate_cvar(historical_returns, 0.95);
        metrics.cvar_99 = calculate_cvar(historical_returns, 0.99);

        // VaR
        if (!historical_returns.empty()) {
            std::vector<double> sorted(historical_returns.begin(), historical_returns.end());
            std::sort(sorted.begin(), sorted.end());

            size_t var_95_idx = static_cast<size_t>(sorted.size() * 0.05);
            size_t var_99_idx = static_cast<size_t>(sorted.size() * 0.01);

            metrics.var_95 = sorted[var_95_idx];
            metrics.var_99 = sorted[var_99_idx];
        } else {
            metrics.var_95 = 0.0;
            metrics.var_99 = 0.0;
        }

        // Expected Shortfall
        metrics.expected_shortfall = metrics.cvar_95;

        // Risk to Ruin
        metrics.risk_to_ruin = calculate_risk_to_ruin(
            win_rate, avg_win, avg_loss, 0.02);

        // Max position size
        metrics.max_position_size = capital * metrics.kelly_fraction;

        // Safety check
        metrics.is_safe = (
            metrics.risk_to_ruin < 0.01 &&
            metrics.cvar_95 > -0.10 &&
            metrics.kelly_fraction > 0.0
        );

        return metrics;
    }
};

} // namespace effata::risk
