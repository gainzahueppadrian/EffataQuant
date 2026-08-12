#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include "../core/quantum_data.hpp"

namespace effata::risk {

using namespace effata::core;

class EVTRisk {
public:
    struct GPDParams { double xi, beta; };

    static GPDParams fit_mle(const std::vector<double>& excesses,
                             int max_iter = 50, double tol = 1e-8) {
        if (excesses.empty()) return {0.1, 0.1};
        double m1 = 0, m2 = 0;
        for (double x : excesses) { m1 += x; m2 += x*x; }
        m1 /= excesses.size(); m2 /= excesses.size();
        double var = m2 - m1*m1;

        if (var <= 0.0) return {0.1, std::max(m1, 1e-9)};

        double xi = 0.5 * (1 - m1*m1 / var);
        double beta = 0.5 * m1 * (1 + m1*m1 / var);
        if (beta < 1e-9) beta = 1e-3;
        const int n = excesses.size();

        for (int it = 0; it < max_iter; ++it) {
            double g0 = 0, g1 = 0, h00 = 0, h11 = 0;
            for (double y : excesses) {
                double t = 1.0 + xi * y / beta;
                if (t < 1e-9) { xi *= 0.5; beta *= 1.1; continue; }
                double log_t = std::log(t);
                double yb = y / beta;
                g0 += log_t / (xi*xi) - (1.0/xi + 1.0) * yb / t;
                h00 += -2.0 * log_t / (xi*xi*xi);
                h11 += n / (double(n) * beta * beta);
            }
            g1 = -n / beta + (1.0/xi + 1.0) * xi / beta *
                 std::accumulate(excesses.begin(), excesses.end(), 0.0,
                                 [&](double a, double y){ return a + y/beta/(1+xi*y/beta); });
            double step_xi = -g0 / (std::abs(h00) + 1e-6);
            double step_b  = -g1 / (std::abs(h11) + 1e-6);
            step_xi = std::clamp(step_xi, -0.3, 0.3);
            step_b  = std::clamp(step_b,  -beta*0.5, beta*0.5);
            xi += step_xi;
            beta += step_b;
            if (beta < 1e-9) beta = 1e-9;
            if (std::abs(step_xi) + std::abs(step_b) < tol) break;

        }
        return {xi, std::max(beta, 1e-9)};
    }

    static double expected_shortfall(double u, const GPDParams& p, double alpha) {
        if (p.xi >= 1.0) return std::numeric_limits<double>::infinity();
        return u / (1.0 - p.xi) + p.beta / (1.0 - p.xi);
    }

    static uint32_t size_position(double portfolio, double price,
                                  double es, const ModelParams& mp) {
        double max_loss = portfolio * mp.max_loss_pct;
        double risk_per_contract = std::max(es * 100.0, 1e-6);
        uint32_t qty = static_cast<uint32_t>(max_loss / risk_per_contract);
        return std::min(qty, 50u);
    }
};

} // namespace effata::risk
