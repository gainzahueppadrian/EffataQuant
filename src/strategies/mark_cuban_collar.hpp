// effata_v32/strategies/mark_cuban_collar.hpp
#pragma once
#include <cmath>
#include <expected>
#include <concepts>
#include <vector>
#include <algorithm>
#include <string>

// Include the Financial Hamiltonian for True Econophysics pricing
#include "../pricing/aad_greeks.hpp"

namespace effata::econophysics {

struct CollarResult {
    double spot_price;
    double call_strike;
    double put_strike;
    double call_premium;
    double put_premium;
    double net_cost;
    double spread_ratio;
    double floor_value;
    double cap_value;
    double protection_pct;
    bool is_zero_cost;
    bool is_upside_covered;
    std::string reasoning;
};

class MarkCubanCollar {
public:
    /**
     * Calcula collar óptimo con restricciones de Mark Cuban (Yahoo 1999)
     * Utiliza el modelo Hamiltonian Econophysics
     */
    [[nodiscard]] static std::expected<CollarResult, std::string> calculate_optimal_collar(
        double spot,
        double call_strike,
        double call_premium,
        double max_cost_pct = 0.0,
        double min_protection_pct = 0.10) {

        CollarResult result{};
        result.spot_price = spot;
        result.call_strike = call_strike;
        result.call_premium = call_premium;

        if (call_strike <= spot) {
            return std::unexpected("Call strike must be OTM (call_strike > spot)");
        }

        double upside_spread = call_strike - spot;
        result.cap_value = call_strike;

        double max_put_cost = call_premium * max_cost_pct;

        double best_put_strike = 0.0;
        double best_put_premium = 0.0;
        double best_protection = 0.0;

        double low = spot * 0.50;
        double high = spot * 0.99;

        for (int iter = 0; iter < 100; ++iter) {
            double mid = (low + high) / 2.0;

            // Reemplazo de Black-Scholes por el Hamiltonian Econophysics Model
            // Parámetros: S, K, T=30/365, v=0.25 (UKF impl), Vol=10k, OI=50k, is_call=false
            double T = 30.0 / 365.0;
            double estimated_volatility = 0.25;
            double strike_volume = 10000.0;
            double strike_oi = 50000.0;

            auto greeks = berkshire::pricing::AADEngine::compute_hamiltonian(
                spot, mid, T, estimated_volatility, strike_volume, strike_oi, false);

            double put_premium = std::max(0.0, greeks.price);

            double net_cost = put_premium - call_premium;
            double protection = (spot - mid) / spot;

            if (net_cost <= max_put_cost && protection >= min_protection_pct) {
                best_put_strike = mid;
                best_put_premium = put_premium;
                best_protection = protection;
                low = mid;
            } else {
                high = mid;
            }
        }

        if (best_put_strike == 0.0) {
            return std::unexpected("No valid put strike found within constraints");
        }

        result.put_strike = best_put_strike;
        result.put_premium = best_put_premium;
        result.net_cost = best_put_premium - call_premium;
        result.floor_value = best_put_strike;
        result.protection_pct = best_protection;

        double downside_spread = spot - best_put_strike;
        result.spread_ratio = upside_spread / downside_spread;

        result.is_zero_cost = (std::abs(result.net_cost) < 0.01);
        result.is_upside_covered = (result.spread_ratio >= 1.0);

        result.reasoning = generate_reasoning(result);

        return result;
    }

    [[nodiscard]] static int calculate_puts_needed(
        double call_premium,
        double put_premium,
        double spread_ratio) {

        if (put_premium <= 0.0 || spread_ratio <= 0.0) return 0;

        double ratio = call_premium / put_premium;
        int n_puts = static_cast<int>(std::ceil(ratio / spread_ratio));

        return std::max(1, n_puts);
    }

    [[nodiscard]] static bool validate_buffett_compliance(const CollarResult& result) {
        if (!result.is_upside_covered) return false;
        if (result.protection_pct < 0.10) return false;
        double max_cost = result.spot_price * 0.02;
        if (result.net_cost > max_cost) return false;
        if (result.spread_ratio < 1.0) return false;

        return true;
    }

private:
    static std::string generate_reasoning(const CollarResult& result) {
        std::string reason = "COLLAR OPTIMIZED (HAMILTONIAN PRICING): ";
        reason += "Spot=$" + std::to_string(result.spot_price) + ", ";
        reason += "Call=" + std::to_string(result.call_strike) + " (Premium=$" +
                  std::to_string(result.call_premium) + "), ";
        reason += "Put=" + std::to_string(result.put_strike) + " (Premium=$" +
                  std::to_string(result.put_premium) + "), ";
        reason += "Net Cost=$" + std::to_string(result.net_cost) + ", ";
        reason += "Spread Ratio=" + std::to_string(result.spread_ratio) + ", ";
        reason += "Protection=" + std::to_string(result.protection_pct * 100) + "%, ";
        reason += "Zero Cost=" + std::string(result.is_zero_cost ? "YES" : "NO") + ", ";
        reason += "Upside Covered=" + std::string(result.is_upside_covered ? "YES" : "NO");

        return reason;
    }
};

} // namespace effata::econophysics
