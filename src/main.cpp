#include <iostream>
#include <thread>
#include "core/ring_buffer.hpp"
#include "regime/dgmh_eigen.hpp"
#include "pricing/hamiltonian.hpp"
#include "ibkr/ibkr_wrapper.hpp"

using namespace berkshire;

int main() {
    std::cout << "Starting Effata Investments HFT Engine..." << std::endl;

    // 1. Initialize components
    core::RingBuffer<Eigen::VectorXd, 1024> market_data_queue;

    regime::DGMHEigen::Config dgmh_config;
    dgmh_config.n_states = 3;
    dgmh_config.n_features = 2;
    dgmh_config.n_components = 2;
    regime::DGMHEigen regime_engine(dgmh_config);

    pricing::HamiltonianEngine pricing_engine;
    ibkr::IBKRWrapper ibkr("127.0.0.1", 7497, 1);

    // Simulate offline training
    std::cout << "Training DGMH Engine offline..." << std::endl;
    Eigen::MatrixXd hist_obs = Eigen::MatrixXd::Random(1000, 2);
    regime_engine.train(hist_obs);

    ibkr.connect();

    std::atomic<bool> running{true};

    // 2. Data Ingestion Thread (Producer)
    std::thread data_feed([&]() {
        for (int i = 0; i < 1000; ++i) {
            Eigen::VectorXd obs(2);
            obs << 100.0 + (rand() % 10), 0.05 + (rand() % 10) / 100.0;

            while (!market_data_queue.push(obs) && running) {
                core::cpu_relax();
            }
        }
        running = false;
    });

    // 3. Trading Engine Thread (Consumer)
    std::thread trading_engine([&]() {
        std::vector<pricing::HamiltonianEngine::OptionData> calls = {
            {105.0, 2.0, 1000, 5000, 0.05}
        };
        std::vector<pricing::HamiltonianEngine::OptionData> puts = {
            {95.0, 4.0, 1500, 6000, 0.06}
        };

        while (running || !market_data_queue.empty_heuristic()) {
            Eigen::VectorXd obs;
            if (market_data_queue.pop(obs)) {
                // Low latency online Viterbi inference
                int state = regime_engine.filter_online(obs);

                if (regime_engine.regime_change_detected()) {
                    std::cout << "Regime Change Detected! New State: " << state << std::endl;
                    regime_engine.reset_regime_change_flag();
                }

                double price = obs(0);

                // Low latency options pricing
                double force = pricing_engine.compute_probable_direction(price, calls);

                // Execute if conditions met (Warren Buffet risk check simulated)
                if (force > 0 && state == 0) {
                    ibkr.place_order("SPY", "BUY", 100, price);
                }
            } else {
                core::cpu_relax();
            }
        }
    });

    data_feed.join();
    trading_engine.join();
    ibkr.disconnect();

    std::cout << "System shutdown gracefully." << std::endl;
    return 0;
}
