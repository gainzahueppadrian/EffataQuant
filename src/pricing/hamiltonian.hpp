#pragma once

#include <cmath>
#include <vector>
#include <Eigen/Dense>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::pricing {

/**
 * @brief Financial Hamiltonian Econophysics Model
 * Replaces Black-Scholes-Merton. Models price S as a particle in a gravitational well
 * created by liquidity (Open Interest & Volume) at different Strikes K.
 */
class HamiltonianEngine {
public:
    struct OptionData {
        double strike;
        double premium;
        double volume;
        double open_interest;
        double gamma; // Precomputed or estimated
    };

    /**
     * @brief Computes the Liquidity Force F_L(K) exerted by a strike on the current price
     * Formula: F_L(K) = kappa * (OI_K * Vol_K * Premium_K) / (|S - K| + epsilon) * Gamma_K
     */
    ALWAYS_INLINE double compute_liquidity_force(double current_price, const OptionData& option, double kappa = 1.0, double epsilon = 1e-4) const {
        double mass = option.open_interest * option.volume * option.premium;
        double distance = std::abs(current_price - option.strike) + epsilon;
        return kappa * (mass / distance) * option.gamma;
    }

    /**
     * @brief Computes the Vector of Probable Direction (Aggregated F_L)
     */
    HOT double compute_probable_direction(double current_price, const std::vector<OptionData>& chain) const {
        double total_force = 0.0;

        // Potential for SIMD here if we split OptionData into SoA (Struct of Arrays)
        for (const auto& opt : chain) {
            double force = compute_liquidity_force(current_price, opt);
            // Directional force: if strike > price, pulls up (+), else pulls down (-)
            if (opt.strike > current_price) {
                total_force += force;
            } else {
                total_force -= force;
            }
        }
        return total_force;
    }

    /**
     * @brief Computes Gamma Exposure (GEX) for a single option
     */
    ALWAYS_INLINE double compute_gex(double current_price, const OptionData& option, bool is_call) const {
        double gex = option.gamma * option.open_interest * 100.0 * current_price;
        return is_call ? gex : -gex;
    }

    /**
     * @brief Computes Aggregated GEX for the entire chain
     */
    HOT double compute_total_gex(double current_price, const std::vector<OptionData>& calls, const std::vector<OptionData>& puts) const {
        double total_gex = 0.0;
        for (const auto& c : calls) {
            total_gex += compute_gex(current_price, c, true);
        }
        for (const auto& p : puts) {
            total_gex += compute_gex(current_price, p, false);
        }
        return total_gex;
    }
};

} // namespace berkshire::pricing
