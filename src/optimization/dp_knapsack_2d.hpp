// effata_v32/optimization/dp_knapsack_2d.hpp
#pragma once
#include <vector>
#include <algorithm>
#include <expected>
#include <cmath>

namespace effata::optimization {

struct StrategyOption {
    int id;
    std::string ticker;
    std::string strategy_type;  // "COLLAR", "IRON_CONDOR", "JADE_LIZARD", etc.
    double premium_collected;
    double max_loss;
    double expected_return;
    double win_rate;
    double capital_required;
    double theta_positive;
    double vega_exposure;
    double gamma_risk;
};

struct KnapsackResult2D {
    std::vector<int> selected_strategies;
    double total_premium;
    double total_capital_used;
    double expected_return;
    double win_rate;
    double sharpe_ratio;
    double max_drawdown;
    bool is_optimal;
};

/**
 * DP Knapsack 2D con Convex Hull Optimization
 *
 * Dimensión 1: Capital disponible (C)
 * Dimensión 2: Riesgo máximo permitido (R)
 *
 * Objetivo: Maximizar Premium Total sujeto a:
 * - Sum(Capital_i) <= C
 * - Sum(MaxLoss_i) <= R
 * - Win Rate promedio >= 0.60
 */
class DPKnapsack2D {
private:
    static constexpr double MIN_WIN_RATE = 0.60;
    static constexpr int MAX_STRATEGIES = 100;

public:
    [[nodiscard]] static KnapsackResult2D solve(
        const std::vector<StrategyOption>& strategies,
        double capital_available,
        double max_risk_allowed) {

        int n = strategies.size();
        if (n == 0 || capital_available <= 0.0 || max_risk_allowed <= 0.0) {
            return KnapsackResult2D{{}, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false};
        }

        // Discretizar dimensiones
        int C = static_cast<int>(capital_available / 1000.0);  // $1k units
        int R = static_cast<int>(max_risk_allowed / 500.0);    // $500 units

        // DP table: dp[c][r] = max premium with capital c and risk r
        std::vector<std::vector<double>> dp(C + 1, std::vector<double>(R + 1, 0.0));
        std::vector<std::vector<std::vector<int>>> choices(C + 1,
            std::vector<std::vector<int>>(R + 1));

        // Fill DP table
        for (int i = 0; i < n; ++i) {
            int cap_i = static_cast<int>(strategies[i].capital_required / 1000.0);
            int risk_i = static_cast<int>(strategies[i].max_loss / 500.0);
            double prem_i = strategies[i].premium_collected;

            // Iterate backwards to avoid using same item twice
            for (int c = C; c >= cap_i; --c) {
                for (int r = R; r >= risk_i; --r) {
                    double new_value = dp[c - cap_i][r - risk_i] + prem_i;

                    if (new_value > dp[c][r]) {
                        dp[c][r] = new_value;
                        choices[c][r] = choices[c - cap_i][r - risk_i];
                        choices[c][r].push_back(i);
                    }
                }
            }
        }

        // Backtrack to find selected strategies
        std::vector<int> selected = choices[C][R];

        // Calculate metrics
        double total_premium = 0.0;
        double total_capital = 0.0;
        double total_return = 0.0;
        double weighted_win_rate = 0.0;

        for (int idx : selected) {
            total_premium += strategies[idx].premium_collected;
            total_capital += strategies[idx].capital_required;
            total_return += strategies[idx].expected_return;
            weighted_win_rate += strategies[idx].win_rate * strategies[idx].capital_required;
        }

        if (total_capital > 0.0) {
            weighted_win_rate /= total_capital;
        }

        // Calculate Sharpe ratio
        double risk_free_rate = 0.05;
        double volatility = std::sqrt(total_premium * 0.10);  // Approximation
        double sharpe = (total_return - risk_free_rate) / volatility;

        // Calculate max drawdown
        double max_drawdown = 0.0;
        for (int idx : selected) {
            max_drawdown = std::max(max_drawdown, strategies[idx].max_loss / strategies[idx].capital_required);
        }

        KnapsackResult2D result;
        result.selected_strategies = selected;
        result.total_premium = total_premium;
        result.total_capital_used = total_capital;
        result.expected_return = total_return;
        result.win_rate = weighted_win_rate;
        result.sharpe_ratio = sharpe;
        result.max_drawdown = max_drawdown;
        result.is_optimal = (weighted_win_rate >= MIN_WIN_RATE && selected.size() > 0);

        return result;
    }

    /**
     * Convex Hull Optimization para filtrar estrategias dominadas
     */
    [[nodiscard]] static std::vector<StrategyOption> apply_convex_hull(
        const std::vector<StrategyOption>& strategies) {

        if (strategies.size() <= 2) return strategies;

        // Sort by premium descending
        std::vector<StrategyOption> sorted = strategies;
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) {
                return a.premium_collected > b.premium_collected;
            });

        std::vector<StrategyOption> hull;

        for (const auto& strat : sorted) {
            // Check if dominated by existing strategies
            bool dominated = false;

            for (const auto& h : hull) {
                // Dominated if lower premium AND higher risk
                if (h.premium_collected >= strat.premium_collected &&
                    h.max_loss <= strat.max_loss &&
                    h.win_rate >= strat.win_rate) {
                    dominated = true;
                    break;
                }
            }

            if (!dominated) {
                // Remove strategies dominated by new one
                hull.erase(
                    std::remove_if(hull.begin(), hull.end(),
                        [&strat](const auto& h) {
                            return strat.premium_collected >= h.premium_collected &&
                                   strat.max_loss <= h.max_loss &&
                                   strat.win_rate >= h.win_rate;
                        }),
                    hull.end());

                hull.push_back(strat);
            }
        }

        return hull;
    }
};

} // namespace effata::optimization
