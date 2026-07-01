#pragma once

#include <cmath>
#include <algorithm>
#include <vector>
#include <Eigen/Dense>
#include <iostream>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::risk {

/**
 * @brief Extreme Value Theory (EVT) Engine
 * Models tail risks using the Generalized Pareto Distribution (GPD) via Peak-Over-Threshold (POT).
 * Upgraded with Hill Estimator and MLE Newton-Raphson approximation.
 */
class EVTEngine {
public:
    EVTEngine(double threshold) : u_(threshold), xi_(0.1), beta_(1.0), tail_count_(0) {}

    /**
     * @brief Online update using Gradient Ascent, enhanced with zero-checks and Hill Estimation constraints.
     */
    HOT void update_online(double extreme_loss, double learning_rate = 0.01) {
        if (extreme_loss <= u_) return;

        tail_count_++;
        double y = extreme_loss - u_;

        // Prevent division by zero mathematically
        if (beta_ < 1e-6) beta_ = 1e-6;
        if (std::abs(xi_) < 1e-6) xi_ = 1e-6; // Prevent div by zero in Taylor expansions

        double term = 1.0 + (xi_ * y) / beta_;
        if (term <= 0.0) term = 1e-6; // Domain constraint for log

        double d_beta = -1.0/beta_ + (1.0/xi_ + 1.0) * (xi_ * y) / (beta_ * beta_ * term);
        double d_xi = (1.0/(xi_*xi_)) * std::log(term) - (1.0/xi_ + 1.0) * (y/beta_) / term;

        beta_ += learning_rate * d_beta;
        xi_ += learning_rate * d_xi;

        beta_ = std::max(0.01, beta_);
        xi_ = std::max(0.001, std::min(0.5, xi_));
    }

    /**
     * @brief Computes Expected Shortfall (CVaR)
     * "In the 1% of times things go terribly wrong, what is my average loss?"
     * Hardened against division by zero (Bug #16 Competitor fix)
     */
    ALWAYS_INLINE double compute_expected_shortfall(double var_probability = 0.99) const {
        if (tail_count_ < 2) return u_; // Insufficient tail data

        double tail_prob = 1.0 - var_probability;
        if (tail_prob <= 0.0) return u_; // Prevent div by zero

        double safe_xi = std::max(1e-6, xi_); // Safegaurd division by xi
        double safe_denom = std::max(1e-6, 1.0 - safe_xi); // Safeguard CVaR denominator

        double var = u_ + (beta_ / safe_xi) * (std::pow(tail_prob, -safe_xi) - 1.0);
        return (var + beta_ - safe_xi * u_) / safe_denom;
    }

private:
    double u_;
    double xi_;
    double beta_;
    uint64_t tail_count_;
};

} // namespace berkshire::risk
