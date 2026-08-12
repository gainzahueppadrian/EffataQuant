#include <cstring>
#include "memory/numa_memory_pool.hpp"
#include "memory/hft_allocator.hpp"
#include "core/quantum_data.hpp"
#include "pricing/bates_pricer.hpp"
#include "ml/robust_hmm.hpp"
#include "risk/evt_engine.hpp"
#include "analytics/gex_cache.hpp"
#include "agents/async_llm.hpp"
#include "core/cache_aligned_lock_free_queue.hpp"

#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>

using namespace effata;
using namespace effata::core;

int main() {
    std::cout << "\n[QUANTUM ENGINE v2.0] Inicializando sistema de alto rendimiento...\n";

    ModelParams params{
        .kappa = 1.2, .theta = 0.04, .xi = 0.3, .rho = -0.7,
        .lambda_j = 0.5, .mu_j = -0.05, .sigma_j = 0.15,
        .r = 0.05, .q = 0.01
    };
    params.sigma_j_sq = params.sigma_j * params.sigma_j;
    params.kappa_j_comp = std::exp(params.mu_j + 0.5 * params.sigma_j_sq) - 1.0;

    ml::RobustHMM hmm(3, 2);
    analytics::GEXCache gex_cache;
    agents::AsyncLLM llm;

    std::vector<MarketData> chain;
    chain.reserve(1024);

    std::vector<double> recent_returns;
    recent_returns.reserve(10000);
    double last_spot = 0;
    int hmm_state = 1;
    uint64_t tick = 0;
    auto heartbeat = std::chrono::steady_clock::now();

    std::atomic<bool> alive{true};

    // Simulate some simple execution loop for benchmark integration
    MarketData md{};
    std::memset(&md, 0, sizeof(MarketData));;
    md.spot = 2000.0;
    md.strike = 2500.0;
    md.iv = 0.25;
    md.oi = 1000;
    md.volume = 500;
    md.expiry_days = 30;

    chain.push_back(md);

    std::cout << "[INIT] Módulos analíticos cargados con éxito.\n";

    while (alive.load(std::memory_order_relaxed)) {
        tick++;

        if (tick % 100 == 0) {
            double v0 = 0.04;
            gex_cache.update_chain(chain, md.spot, tick, params, v0);
            auto gex = gex_cache.compute(md.spot, chain);

            Eigen::VectorXd obs(2);
            double ret = recent_returns.empty() ? 0.0 : recent_returns.back();
            obs << ret, md.iv;
            hmm_state = hmm.filter_step(hmm_state, obs);

            std::string prompt = "G:" + std::to_string(gex.net_gex) +
                                 "|F:" + std::to_string(gex.flip_zone) +
                                 "|S:" + std::to_string(hmm_state);
            llm.send_async(prompt);

            if (recent_returns.size() > 100) {
                std::vector<double> excesses;
                double threshold = 0.01;
                for (double r : recent_returns)
                    if (r < -threshold) excesses.push_back(-r - threshold);
                if (excesses.size() > 30) {
                    auto gpd = risk::EVTRisk::fit_mle(excesses);
                    double es = risk::EVTRisk::expected_shortfall(
                        threshold, gpd, params.es_confidence);
                    uint32_t qty = risk::EVTRisk::size_position(
                        1'000'000, 1.0, es, params);
                    (void)qty; // Prevent unused warning
                }
            }
        }

        if (auto resp = llm.try_recv()) {
            // Log response simulated
        }

        if (std::chrono::steady_clock::now() - heartbeat > std::chrono::seconds(1)) {
            heartbeat = std::chrono::steady_clock::now();
            break; // Stop loop after 1 second of simulation for benchmark completion
        }
    }

    std::cout << "[SHUTDOWN] Sistema cerrado limpiamente. Ticks procesados: " << tick << "\n";
    return 0;
}
