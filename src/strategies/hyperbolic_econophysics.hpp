// effata_v31/core/hyperbolic_econophysics.hpp
#pragma once
#include <cmath>
#include <immintrin.h>
#include <expected>
#include <concepts>
#include <array>

namespace effata::econophysics {

/**
 * HYPERBOLIC R-MULTIPLE FORMULA (Martin Luk + Dan Zanger + Asymmetric Edge)
 *
 * Core Insight: Cutting stop from 3% to 1.5% DOUBLES position size at same dollar risk
 * This creates a PARABOLIC/HYPERBOLIC relationship in returns
 *
 * R-Multiple = Trade Return / Stop-Loss Width
 *
 * Hyperbolic Factor: H = 1 / (Stop-Loss Width)^α where α ∈ [1.0, 2.0]
 *
 * Combined with Kelly + CVaR + Risk to Ruin:
 *
 * Optimal Position Size = Kelly(f*) × Hyperbolic Factor × Risk Adjustment
 *
 * Expected Value = Win Rate × Avg Win - Loss Rate × Avg Loss
 *
 * Risk to Ruin = (1 - Edge)^(Capital / Risk per Trade)
 */

struct HyperbolicConfig {
    double base_stop_width{0.03};      // 3% baseline
    double tight_stop_width{0.015};    // 1.5% tight stop (Martin Luk)
    double hyperbolic_exponent{1.5};   // α for hyperbolic factor
    double kelly_fraction{0.25};       // Quarter Kelly (conservative)
    double cvar_confidence{0.95};      // 95% CVaR
    double max_risk_per_trade{0.005};  // 0.5% per trade (Martin Luk)
    double win_rate{0.22};             // 22% win rate (Martin Luk)
    double avg_win_multiple{6.0};      // 6:1 reward/risk ratio
};

struct RiskMetrics {
    double r_multiple;
    double hyperbolic_factor;
    double position_size_pct;
    double expected_value;
    double risk_to_ruin;
    double kelly_optimal;
    double cvar_95;
    double sharpe_ratio;
    double sortino_ratio;
    double calmar_ratio;
    double max_drawdown;
    bool is_viable;
    std::string reasoning;
};

class HyperbolicEconophysicsEngine {
private:
    HyperbolicConfig config_;

public:
    explicit HyperbolicEconophysicsEngine(const HyperbolicConfig& config)
        : config_(config) {}

    /**
     * HYPERBOLIC R-MULTIPLE CALCULATION
     *
     * Key Insight: Tighter stop = Higher R-Multiple = Parabolic Returns
     *
     * R = Trade Return / Stop Width
     *
     * Example (Martin Luk):
     * - Stop 3%: Position = 0.5% / 3% = 16.67% of capital
     * - Stop 1.5%: Position = 0.5% / 1.5% = 33.33% of capital (DOUBLED!)
     * - If trade wins 15%: R = 15% / 1.5% = 10R (vs 5R with 3% stop)
     */
    [[nodiscard]] double calculate_r_multiple(
        double trade_return_pct,
        double stop_width_pct) const noexcept {

        if (stop_width_pct <= 0.0) [[unlikely]] return 0.0;
        return trade_return_pct / stop_width_pct;
    }

    /**
     * HYPERBOLIC FACTOR: The Mathematical Edge
     *
     * H = (Base Stop / Tight Stop)^α
     *
     * This factor shows how much MORE you can profit with tighter stops
     *
     * Example:
     * - Base Stop: 3%
     * - Tight Stop: 1.5%
     * - α = 1.5
     * - H = (3% / 1.5%)^1.5 = 2^1.5 = 2.83x
     *
     * This means 2.83x MORE profit potential with same risk!
     */
    [[nodiscard]] double calculate_hyperbolic_factor(
        double stop_width_pct) const noexcept {

        if (stop_width_pct <= 0.0) [[unlikely]] return 1.0;

        double ratio = config_.base_stop_width / stop_width_pct;
        return std::pow(ratio, config_.hyperbolic_exponent);
    }

    /**
     * KELLY CRITERION: Optimal Position Sizing
     *
     * f* = (p × b - q) / b
     *
     * Where:
     * - p = win probability
     * - q = loss probability (1 - p)
     * - b = win/loss ratio (avg win / avg loss)
     *
     * Martin Luk: 22% win rate, 6:1 ratio
     * f* = (0.22 × 6 - 0.78) / 6 = (1.32 - 0.78) / 6 = 0.09 = 9%
     * Quarter Kelly = 2.25% per trade
     */
    [[nodiscard]] double calculate_kelly_criterion(
        double win_rate,
        double win_loss_ratio) const noexcept {

        double q = 1.0 - win_rate;
        double kelly = (win_rate * win_loss_ratio - q) / win_loss_ratio;
        return std::max(0.0, kelly);
    }

    /**
     * RISK TO RUIN: Probability of Blowing Up
     *
     * R = (1 - Edge)^(Capital / Risk per Trade)
     *
     * Where Edge = Win Rate × Avg Win - Loss Rate × Avg Loss
     *
     * Lower risk to ruin = Higher survival probability
     */
    [[nodiscard]] double calculate_risk_to_ruin(
        double win_rate,
        double avg_win_pct,
        double avg_loss_pct,
        double risk_per_trade_pct) const noexcept {

        double edge = win_rate * avg_win_pct - (1.0 - win_rate) * avg_loss_pct;
        double capital_units = 1.0 / risk_per_trade_pct;

        if (edge <= 0.0) [[unlikely]] return 1.0;  // Certain ruin

        return std::pow(1.0 - edge, capital_units);
    }

    /**
     * CVaR (Conditional Value at Risk): Expected Shortfall
     *
     * CVaR = E[Loss | Loss > VaR]
     *
     * Measures average loss in worst (1-α)% scenarios
     * Critical for fat-tail events
     */
    [[nodiscard]] double calculate_cvar(
        const std::vector<double>& returns,
        double confidence_level) const noexcept {

        if (returns.empty()) [[unlikely]] return 0.0;

        std::vector<double> sorted_returns = returns;
        std::sort(sorted_returns.begin(), sorted_returns.end());

        size_t tail_index = static_cast<size_t>(
            sorted_returns.size() * (1.0 - confidence_level));

        if (tail_index == 0) return sorted_returns[0];

        double tail_sum = 0.0;
        for (size_t i = 0; i < tail_index; ++i) {
            tail_sum += sorted_returns[i];
        }

        return tail_sum / tail_index;
    }

    /**
     * UNIFIED HYPERBOLIC POSITION SIZING
     *
     * Combines:
     * 1. Kelly Criterion (optimal growth)
     * 2. Hyperbolic Factor (tight stop multiplier)
     * 3. Risk to Ruin (survival constraint)
     * 4. CVaR (tail risk protection)
     *
     * Final Position = Kelly × Hyperbolic × Risk Adjustment
     */
    [[nodiscard]] RiskMetrics calculate_optimal_position(
        double current_stop_width_pct,
        double expected_return_pct,
        double capital,
        const std::vector<double>& historical_returns) const {

        RiskMetrics metrics;

        // 1. R-Multiple (Martin Luk's core insight)
        metrics.r_multiple = calculate_r_multiple(
            expected_return_pct, current_stop_width_pct);

        // 2. Hyperbolic Factor (parabolic edge)
        metrics.hyperbolic_factor = calculate_hyperbolic_factor(current_stop_width_pct);

        // 3. Kelly Criterion (optimal sizing)
        double kelly_full = calculate_kelly_criterion(
            config_.win_rate, config_.avg_win_multiple);
        metrics.kelly_optimal = kelly_full;

        // 4. Apply Quarter Kelly (conservative)
        double kelly_adjusted = kelly_full * config_.kelly_fraction;

        // 5. Hyperbolic Multiplier (tight stop advantage)
        double hyperbolic_size = kelly_adjusted * metrics.hyperbolic_factor;

        // 6. Risk to Ruin constraint
        metrics.risk_to_ruin = calculate_risk_to_ruin(
            config_.win_rate,
            expected_return_pct,
            current_stop_width_pct,
            config_.max_risk_per_trade);

        // 7. CVaR from historical returns
        metrics.cvar_95 = calculate_cvar(historical_returns, config_.cvar_confidence);

        // 8. Final position size (constrained)
        metrics.position_size_pct = std::min({
            hyperbolic_size,
            config_.max_risk_per_trade / current_stop_width_pct,  // Risk constraint
            0.33  // Max 33% per position (Martin Luk's max)
        });

        // 9. Expected Value
        metrics.expected_value = config_.win_rate * expected_return_pct -
                                (1.0 - config_.win_rate) * current_stop_width_pct;

        // 10. Viability check
        metrics.is_viable = (
            metrics.expected_value > 0.0 &&
            metrics.risk_to_ruin < 0.01 &&  // < 1% ruin probability
            metrics.position_size_pct > 0.0 &&
            metrics.cvar_95 > -0.10  // CVaR not too negative
        );

        // 11. Generate reasoning
        metrics.reasoning = generate_reasoning(metrics);

        return metrics;
    }

    /**
     * SHARPE RATIO: Risk-Adjusted Return
     *
     * Sharpe = (Return - Risk-Free Rate) / Volatility
     */
    [[nodiscard]] double calculate_sharpe_ratio(
        double portfolio_return,
        double risk_free_rate,
        double volatility) const noexcept {

        if (volatility <= 0.0) [[unlikely]] return 0.0;
        return (portfolio_return - risk_free_rate) / volatility;
    }

    /**
     * SORTINO RATIO: Downside Risk-Adjusted Return
     *
     * Sortino = (Return - Risk-Free Rate) / Downside Deviation
     *
     * Better than Sharpe for asymmetric returns (like Martin Luk's system)
     */
    [[nodiscard]] double calculate_sortino_ratio(
        double portfolio_return,
        double risk_free_rate,
        const std::vector<double>& returns) const noexcept {

        double downside_sum = 0.0;
        int downside_count = 0;

        for (double ret : returns) {
            if (ret < risk_free_rate) {
                downside_sum += std::pow(ret - risk_free_rate, 2);
                downside_count++;
            }
        }

        if (downside_count == 0) [[unlikely]] return 0.0;

        double downside_deviation = std::sqrt(downside_sum / downside_count);

        if (downside_deviation <= 0.0) return 0.0;

        return (portfolio_return - risk_free_rate) / downside_deviation;
    }

    /**
     * CALMAR RATIO: Return / Max Drawdown
     *
     * Calmar = Annualized Return / Max Drawdown
     *
     * Critical for systems with large drawdowns (like Martin Luk's -26% December)
     */
    [[nodiscard]] double calculate_calmar_ratio(
        double annualized_return,
        double max_drawdown) const noexcept {

        if (max_drawdown <= 0.0) [[unlikely]] return 0.0;
        return annualized_return / std::abs(max_drawdown);
    }

private:
    std::string generate_reasoning(const RiskMetrics& metrics) const {
        std::string reason;

        if (!metrics.is_viable) {
            if (metrics.expected_value <= 0.0) {
                reason += "Negative expected value. ";
            }
            if (metrics.risk_to_ruin >= 0.01) {
                reason += "High risk of ruin. ";
            }
            if (metrics.cvar_95 <= -0.10) {
                reason += "Excessive tail risk. ";
            }
            return reason;
        }

        reason += "HYPERBOLIC EDGE ACTIVATED: ";
        reason += "R-Multiple=" + std::to_string(metrics.r_multiple) + ", ";
        reason += "Hyperbolic Factor=" + std::to_string(metrics.hyperbolic_factor) + "x, ";
        reason += "Position Size=" + std::to_string(metrics.position_size_pct * 100) + "%, ";
        reason += "Kelly=" + std::to_string(metrics.kelly_optimal * 100) + "%, ";
        reason += "Risk to Ruin=" + std::to_string(metrics.risk_to_ruin * 100) + "%, ";
        reason += "CVaR=" + std::to_string(metrics.cvar_95 * 100) + "%";

        return reason;
    }
};

} // namespace effata::econophysics
