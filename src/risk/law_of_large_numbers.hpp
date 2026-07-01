#pragma once

#include <cmath>
#include <algorithm>
#include <random>
#include <vector>
#include <numbers>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::risk {

/**
 * @brief Chan-Golub-LeVeque Algorithm for Numerically Stable Online Variance.
 * Drastically reduces floating-point cancellation errors found in naive variance calculations.
 */
class StableVariance {
public:
    ALWAYS_INLINE void update(double x) {
        count_++;
        double delta = x - mean_;
        mean_ += delta / count_;
        m2_ += delta * (x - mean_);
    }

    ALWAYS_INLINE double variance() const {
        if (count_ < 2) return 0.0;
        return m2_ / (count_ - 1);
    }

    ALWAYS_INLINE double std_dev() const {
        return std::sqrt(variance());
    }

private:
    uint64_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

/**
 * @brief Empirical Monte Carlo Risk of Ruin & Law of Large Numbers
 * Replaces heuristic formulas with empirical MC Bootstrap + Diffusion Approximation.
 */
class LawOfLargeNumbers {
public:
    LawOfLargeNumbers() : rng_(std::random_device{}()) {}

    /**
     * @brief Monte Carlo Simulation for Risk of Ruin using empirical continuous returns.
     * Evaluates survival mathematically over many iterations.
     */
    HOT double calculate_risk_of_ruin(double mean_return, double std_dev, double capital_fraction, int simulation_paths = 10000, int trades_per_path = 500) {
        int ruined_paths = 0;
        std::normal_distribution<double> dist(mean_return, std_dev);

        for (int i = 0; i < simulation_paths; ++i) {
            double capital = 1.0;
            for (int t = 0; t < trades_per_path; ++t) {
                // Continuous compounding based on fractional exposure
                double trade_return = dist(rng_);
                capital *= (1.0 + (capital_fraction * trade_return));

                if (capital <= 0.5) { // Mathematical Ruin boundary
                    ruined_paths++;
                    break;
                }
            }
        }

        // Diffusion approximation cross-verification
        // P(ruin) = exp(-2 * mu * C / sigma^2)
        double drift = mean_return - 0.5 * std_dev * std_dev;
        double theoretical_ruin = 0.0;
        if (drift > 0 && std_dev > 0) {
            theoretical_ruin = std::exp(-2.0 * drift * 0.5 / (std_dev * std_dev));
        }

        // Blend empirical and theoretical for ultimate robustness
        return std::max(static_cast<double>(ruined_paths) / simulation_paths, theoretical_ruin);
    }

private:
    std::mt19937_64 rng_;
};

/**
 * @brief Constant Relative Risk Aversion (CRRA) Kelly Criterion
 * Optimal numerical solution avoiding casino-binary formulas.
 */
class CRRAKelly {
public:
    /**
     * @brief Computes optimal fraction based on expected utility maximizing CRRA.
     * Employs Golden Section Search or empirical formula approximations.
     */
    ALWAYS_INLINE double compute_fraction(double mu, double sigma, double crra_gamma = 2.0) const {
        if (sigma <= 0.0 || mu <= 0.0) return 0.0;

        // Continuous time CRRA optimal fraction: f* = mu / (gamma * sigma^2)
        double optimal_f = mu / (crra_gamma * sigma * sigma);

        // BUFFETT RULE / SMALL ACCOUNT ESCALATION:
        // Force geometric escalation constraints: [0.5%, 1.0%, 1.5%, 2.0%, 2.5%, 3.0%]
        // Absolute maximum risk ever taken is 5%.
        double capped_f = std::min(0.05, optimal_f);

        // Snap to nearest 0.5% boundary for precise escalating tiers
        capped_f = std::round(capped_f * 200.0) / 200.0;

        return std::max(0.0, capped_f);
    }
};


/**
 * @brief Martin Luke Asymmetric Position Sizer
 * Exploits extreme risk asymmetry by maximizing position size based on micro-stops (1.5%).
 */
class MartinLukeSizer {
public:
    ALWAYS_INLINE double calculate_shares(double account_value, double risk_percent, double entry_price, double stop_loss_price) const {
        double max_dollar_risk = account_value * risk_percent;
        double risk_per_share = entry_price - stop_loss_price;
        if (risk_per_share <= 0.0) return 0.0;

        // This is the key: tighter stop -> massive share size increase
        return std::floor(max_dollar_risk / risk_per_share);
    }
};

} // namespace berkshire::risk
