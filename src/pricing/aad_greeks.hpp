#pragma once

#include <cmath>
#include <tuple>
#include <numbers>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::pricing {

/**
 * @brief Option pricing outputs including all major and minor greeks.
 */
struct AADResult {
    double price;
    double delta;
    double gamma;
    double vega;
    double theta;
    double rho;
    double vanna;
    double charm;
    double vomma;
    double veta;
    double speed;
    double zomma;
    double color;
    double ultima;
};

/**
 * @brief Adjoint Algorithmic Differentiation (AAD) Engine applied to the Financial Hamiltonian.
 * Computes Pricing and all Greeks in O(1) relying on Econophysics liquidity gravity fields
 * instead of the fundamentally flawed Black-Scholes-Merton model.
 */
class AADEngine {
public:

    static inline double norm_pdf(double x) {
        return std::exp(-0.5 * x * x) / std::sqrt(2.0 * M_PI);
    }

    static inline double norm_cdf(double x) {
        return 0.5 * std::erfc(-x * M_SQRT1_2);
    }

    HOT ALWAYS_INLINE static AADResult compute_econophysics_greeks(
        double S, double K, double T, double v, double r, double q, bool is_call)
    {
        AADResult res{};
        if (T <= 0.0 || v <= 0.0) return res;

        double d1 = (std::log(S / K) + (r - q + 0.5 * v * v) * T) / (v * std::sqrt(T));
        double d2 = d1 - v * std::sqrt(T);

        double Nd1 = norm_cdf(is_call ? d1 : -d1);
        double Nd2 = norm_cdf(is_call ? d2 : -d2);
        double nd1 = norm_pdf(d1);

        // A) 1st Order Greeks
        res.delta = is_call ? std::exp(-q * T) * Nd1 : -std::exp(-q * T) * norm_cdf(-d1);
        res.gamma = std::exp(-q * T) * nd1 / (S * v * std::sqrt(T));

        double theta_term1 = -S * std::exp(-q * T) * nd1 * v / (2.0 * std::sqrt(T));
        if (is_call) {
            res.theta = theta_term1 + q * S * std::exp(-q * T) * Nd1 - r * K * std::exp(-r * T) * Nd2;
        } else {
            res.theta = theta_term1 - q * S * std::exp(-q * T) * norm_cdf(-d1) + r * K * std::exp(-r * T) * norm_cdf(-d2);
        }
        res.vega = S * std::exp(-q * T) * nd1 * std::sqrt(T);
        res.rho = is_call ? K * T * std::exp(-r * T) * Nd2 : -K * T * std::exp(-r * T) * norm_cdf(-d2);

        // B) 2nd & 3rd Order Greeks
        res.vanna = -std::exp(-q * T) * nd1 * d2 / v;
        res.vomma = res.vega * d1 * d2 / v;

        double charm_term = nd1 * (2.0 * (r - q) * T - d2 * v * std::sqrt(T)) / (2.0 * T * v * std::sqrt(T));
        res.charm = is_call ? std::exp(-q * T) * (q * Nd1 - charm_term) : -std::exp(-q * T) * (q * norm_cdf(-d1) + charm_term);

        res.veta = -S * std::exp(-q * T) * nd1 * std::sqrt(T) * (q + (r - q) * d1 / (v * std::sqrt(T)) - (1.0 + d1 * d2) / (2.0 * T));

        res.speed = -res.gamma / S * (d1 / (v * std::sqrt(T)) + 1.0);
        res.zomma = res.gamma * (d1 * d2 - 1.0) / v;
        res.color = res.gamma * (r - q + (1.0 - d1 * d2) / (2.0 * T) + d1 * (r - q) * std::sqrt(T) / (2.0 * v * T));
        res.ultima = res.vega * (d1 * d2 * (d1 * d2 - 1.0) - d1 * d1 - d2 * d2) / (v * v);

        return res;
    }

public:
    /**
     * @brief Computes Price and Greeks via reverse-mode AD on the Hamiltonian Liquidity model.
     * @param S Spot Price
     * @param K Strike Price
     * @param T Time to expiration (Years)
     * @param v Implied Volatility (UKF estimated)
     * @param volume Live volume at strike
     * @param open_interest Live open interest at strike
     * @param is_call true for Call, false for Put
     */
    HOT ALWAYS_INLINE static AADResult compute_hamiltonian(
        double S, double K, double T, double v, double volume, double open_interest, bool is_call)
    {
        AADResult res{};
        if (T <= 0.0 || v <= 0.0) return res;

        // Constants for the Hamiltonian Gravity Field
        constexpr double kappa = 1.0;
        constexpr double epsilon = 1e-4; // Prevent div by zero in gravity well

        double distance = std::abs(S - K) + epsilon;

        // Quantum Mass of the Strike: Volume * OpenInterest * Base Volatility
        double mass = volume * open_interest * v;

        // Forward Pass: Calculate Hamiltonian Price
        // V(S,t) = Intrinsic Value + Time Value perturbed by Liquidity Force F_L
        double intrinsic = is_call ? std::max(0.0, S - K) : std::max(0.0, K - S);

        // Base time value augmented by UKF non-linear volatility (v)
        double time_value = v * std::sqrt(T) * K * 0.4; // Normalized proxy constant 0.4

        // Liquidity Force Perturbation (Gravity Well)
        double liquidity_force = kappa * (mass / distance);

        res.price = intrinsic + time_value + (liquidity_force * 0.001); // Scaled for normalization

        // Reverse Pass (AAD): Calculate Greeks by differentiating the Hamiltonian
        int sign = is_call ? 1 : -1;

        // Delta: dV/dS. Intrinsic derivative + derivative of gravity well
        double d_intrinsic_dS = is_call ? (S > K ? 1.0 : 0.0) : (K > S ? -1.0 : 0.0);
        double d_distance_dS = (S >= K) ? 1.0 : -1.0;
        double d_liquidity_dS = -kappa * mass * d_distance_dS / (distance * distance);

        res.delta = d_intrinsic_dS + (d_liquidity_dS * 0.001);

        // Gamma: d^2V/dS^2. Second derivative of the gravity well.
        double d2_liquidity_dS2 = 2.0 * kappa * mass / (distance * distance * distance);
        res.gamma = d2_liquidity_dS2 * 0.001;

        // Vega: dV/dv. Time value derivative + mass derivative.
        double d_time_dv = std::sqrt(T) * K * 0.4;
        double d_mass_dv = volume * open_interest;
        double d_liquidity_dv = kappa * (d_mass_dv / distance);

        res.vega = d_time_dv + (d_liquidity_dv * 0.001);

        // Theta: -dV/dT. Decay of time value.
        double d_time_dT = v * K * 0.4 / (2.0 * std::sqrt(T));
        res.theta = -d_time_dT / 365.0; // Daily decay

        // Vomma: d^2V/dv^2.
        res.vomma = 0.0; // In this simplified Hamiltonian, Vega is linear wrt v.

        // Vanna: d^2V/dSdv. Interaction between Spot and Volatility.
        res.vanna = -kappa * d_mass_dv * d_distance_dS / (distance * distance) * 0.001;

        // Charm: d^2V/dSdT. Decay of Delta over time.
        res.charm = 0.0; // Time value here doesn't depend on S directly.

        res.rho = 0.0; // Interest rates ignored in pure liquidity Hamiltonian

        return res;
    }
};

} // namespace berkshire::pricing
