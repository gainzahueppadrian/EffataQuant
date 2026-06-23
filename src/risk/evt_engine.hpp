#pragma once

#include <cmath>
#include <algorithm>
#include <vector>
#include <Eigen/Dense>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::risk {

/**
 * @brief Extreme Value Theory (EVT) Engine
 * Models tail risks using the Generalized Pareto Distribution (GPD) via Peak-Over-Threshold (POT).
 */
class EVTEngine {
public:
    EVTEngine(double threshold) : u_(threshold), xi_(0.1), beta_(1.0) {}

    /**
     * @brief Online Stochastic Gradient Descent to update GPD parameters.
     * Fits the tail without re-running optimization on the entire dataset.
     */
    HOT void update_online(double extreme_loss, double learning_rate = 0.01) {
        if (extreme_loss <= u_) return;

        double y = extreme_loss - u_;

        // Gradients of log-likelihood for GPD:
        // L(xi, beta) = -log(beta) - (1/xi + 1) * log(1 + xi*y/beta)
        double term = 1.0 + (xi_ * y) / beta_;

        double d_beta = -1.0/beta_ + (1.0/xi_ + 1.0) * (xi_ * y) / (beta_ * beta_ * term);
        double d_xi = (1.0/(xi_*xi_)) * std::log(term) - (1.0/xi_ + 1.0) * (y/beta_) / term;

        // Gradient Ascent
        beta_ += learning_rate * d_beta;
        xi_ += learning_rate * d_xi;

        // Constraint bounds
        beta_ = std::max(0.01, beta_);
        xi_ = std::max(0.001, std::min(0.5, xi_)); // Keep shape parameter sane for finance
    }

    /**
     * @brief Computes Expected Shortfall (CVaR)
     * "In the 1% of times things go terribly wrong, what is my average loss?"
     */
    ALWAYS_INLINE double compute_expected_shortfall(double var_probability = 0.99) const {
        // ES formula for GPD assuming VaR is already computed or approximated
        // ES = (VaR + beta - xi * u) / (1 - xi)
        // For simplicity in this online engine, we approximate ES directly from tail params
        double tail_prob = 1.0 - var_probability;
        double var = u_ + (beta_ / xi_) * (std::pow(tail_prob, -xi_) - 1.0);
        return (var + beta_ - xi_ * u_) / (1.0 - xi_);
    }

private:
    double u_;     // Threshold
    double xi_;    // Shape parameter (tail fatness)
    double beta_;  // Scale parameter
};

/**
 * @brief Adaptive Kelly Criterion with Entropy Regularization
 */
class KellyEngine {
public:
    /**
     * @brief Calculates optimal fraction of capital to risk.
     * @param win_prob Probability of winning the trade.
     * @param win_loss_ratio Reward / Risk ratio.
     * @param entropy_penalty Penalty for uncertainty to prevent overbetting.
     */
    ALWAYS_INLINE double compute_fraction(double win_prob, double win_loss_ratio, double entropy_penalty = 0.1) const {
        if (win_prob <= 0.0 || win_loss_ratio <= 0.0) return 0.0;

        // Standard Kelly: f* = W - ((1 - W) / R)
        double f_star = win_prob - ((1.0 - win_prob) / win_loss_ratio);

        // Entropy regularization: H(p) = -p*log(p) - (1-p)*log(1-p)
        double entropy = -win_prob * std::log(win_prob) - (1.0 - win_prob) * std::log(1.0 - win_prob);

        // Reduce bet size if entropy (uncertainty) is high
        double adaptive_f = f_star - (entropy_penalty * entropy);

        return std::max(0.0, std::min(0.25, adaptive_f)); // Hard cap at Quarter Kelly for safety
    }
};

} // namespace berkshire::risk