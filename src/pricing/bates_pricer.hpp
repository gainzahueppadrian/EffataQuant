#pragma once
#include <cmath>
#include <complex>
#include <array>
#include <algorithm>
#include "../core/quantum_data.hpp"

// Fallback manual de SIMD si <simd> no está disponible en la versión estándar actual.
// Proveeremos operaciones escalares unrolled si no hay libsimd nativa,
// o podemos usar intrínsecos AVX nativos, pero lo mantendremos estándar.
#include <vector>

#define FORCE_INLINE __attribute__((always_inline)) inline

namespace effata::pricing {

using namespace effata::core;

class BatesPricer {
public:
    static constexpr size_t N_GL = 20;
    alignas(64) static inline const double gl_nodes[N_GL] = {
        0.0565, 0.2956, 0.7339, 1.3779, 2.2380, 3.3248, 4.6500, 6.2283,
        8.0767, 10.2150, 12.6663, 15.4578, 18.6226, 22.1996, 26.2357, 30.7878,
        35.9258, 41.7382, 48.3459, 55.9300
    };
    alignas(64) static inline const double gl_weights[N_GL] = {
        0.1285, 0.2839, 0.4118, 0.4943, 0.5194, 0.4809, 0.3806, 0.2303,
        0.1073, 0.0352, 0.0076, 0.0010, 0.0001, 0.0000, 0.0000, 0.0000,
        0.0000, 0.0000, 0.0000, 0.0000
    };

    FORCE_INLINE std::complex<double> characteristic_at_half(
        double u, double T, double v0, const ModelParams& p
    ) const noexcept {
        const std::complex<double> I(0.0, 1.0);
        const std::complex<double> z = std::complex<double>(u, 0.0) - std::complex<double>(0.0, 0.5);
        const std::complex<double> z2 = z * z;

        const std::complex<double> alpha = p.kappa * p.theta / (p.xi * p.xi);
        const std::complex<double> beta  = p.kappa - p.rho * p.xi * z * I;
        const double gamma_h = p.xi * p.xi * 0.5;
        const std::complex<double> d = std::sqrt(beta*beta + 2.0 * gamma_h * z2);

        const std::complex<double> g = (beta - d) / (beta + d);
        const std::complex<double> exp_dT = std::exp(-d * T);
        const std::complex<double> one_minus_g_exp = std::complex<double>(1.0, 0.0) - g * exp_dT;

        const std::complex<double> D = (beta - d) / (p.xi * p.xi) *
                        (std::complex<double>(1.0,0.0) - exp_dT) / one_minus_g_exp;
        const std::complex<double> C = alpha * ((beta - d) * T -
                        2.0 * std::log(one_minus_g_exp / (std::complex<double>(1.0,0.0) - g)));

        const std::complex<double> jump_cf = std::exp(
            p.lambda_j * T * (
                std::exp(z * p.mu_j - 0.5 * p.sigma_j_sq * z2) - 1.0
                - z * p.kappa_j_comp
            )
        );

        return std::exp(C + D * v0) * jump_cf; // Added missing jump_cf application
    }

    FORCE_INLINE double price_calls(
        double S, double K, double T, double v0, const ModelParams& p
    ) const noexcept {
        const double fwd = S * std::exp((p.r - p.q) * T);
        const double discount_K = std::exp(-p.r * T);
        const double log_S = std::log(S);
        const double k = std::log(K) - log_S;

        double integral = 0.0;
        for (size_t i = 0; i < N_GL; ++i) {
            const double u = gl_nodes[i];
            const std::complex<double> phi = characteristic_at_half(u, T, v0, p);

            const std::complex<double> I(0.0, 1.0);
            const std::complex<double> phase = std::exp(-I * u * k);
            const double denom = u * u + 0.25;
            const std::complex<double> integrand = phase * phi / denom;

            integral += std::real(integrand) * gl_weights[i];
        }

        const double intrinsic = fwd - K * discount_K;
        const double option_value = std::max(integral * (1.0 / M_PI), 0.0);
        const double price = (intrinsic * 0.5 + option_value * K * discount_K);
        return std::max(price, 0.0);
    }

    FORCE_INLINE double gamma(
        double S, double K, double T, double v0, const ModelParams& p
    ) const noexcept {
        const double h = S * 1e-3;
        const double c_up   = price_calls(S + h, K, T, v0, p);
        const double c_mid  = price_calls(S,     K, T, v0, p);
        const double c_down = price_calls(S - h, K, T, v0, p);
        return (c_up - 2.0 * c_mid + c_down) / (h * h);
    }
};

} // namespace effata::pricing
