#include <iostream>
#include <thread>
#include <cstdlib>
#include "core/ring_buffer.hpp"
#include "core/messages.hpp"
#include "regime/dgmh_eigen.hpp"
#include "pricing/hamiltonian.hpp"
#include "ibkr/ibkr_wrapper.hpp"
#include "execution/smart_router.hpp"
#include "execution/alpha_evolve_fsm.hpp"
#include "risk/compliance_guard.hpp"
#include "risk/evt_engine.hpp"
#include "ipc/zmq_reactor.hpp"

using namespace berkshire;

int main() {
    std::cout << "Starting Effata Investments Complete HFT Engine..." << std::endl;

    // 1. Core Modules
    core::RingBuffer<Eigen::VectorXd, 1024> market_data_queue;
    ipc::ZMQReactor reactor("tcp://127.0.0.1:5555");
    reactor.start();

    // 2. Machine Learning Regime Detection
    regime::DGMHEigen::Config dgmh_config;
    dgmh_config.n_states = 3;
    dgmh_config.n_features = 2;
    dgmh_config.n_components = 2;
    regime::DGMHEigen regime_engine(dgmh_config);

    // 3. Risk & Pricing
    pricing::HamiltonianEngine pricing_engine;
    risk::EVTEngine evt_engine(100.0); // Extreme loss threshold
    risk::KellyEngine kelly_engine;

    // 4. Execution & Routing
    ibkr::IBKRWrapper ibkr("127.0.0.1", 7497, 1);
    execution::SmartOrderRouter router(3); // 3 Brokers (e.g., IBKR, Lightspeed, Schwab)
    execution::AlphaEvolveFSM alpha_fsm;

    // Simulate offline training
    std::cout << "Training DGMH Engine offline..." << std::endl;
    Eigen::MatrixXd hist_obs = Eigen::MatrixXd::Random(1000, 2);
    regime_engine.train(hist_obs);

    ibkr.connect();
    std::atomic<bool> running{true};

    // Data Ingestion Thread (Producer)
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

    // Trading Engine Thread (Consumer)
    std::thread trading_engine([&]() {
        std::vector<pricing::HamiltonianEngine::OptionData> calls = { {105.0, 2.0, 1000, 5000, 0.05} };
        std::vector<pricing::HamiltonianEngine::OptionData> puts = { {95.0, 4.0, 1500, 6000, 0.06} };

        while (running || !market_data_queue.empty_heuristic()) {
            reactor.poll_commands();

            Eigen::VectorXd obs;
            if (market_data_queue.pop(obs)) {
                evt_engine.update_online(std::abs(obs(0) - 100.0));

                int state = regime_engine.filter_online(obs);
                if (regime_engine.regime_change_detected()) {
                    regime_engine.reset_regime_change_flag();
                }

                double price = obs(0);
                double force = pricing_engine.compute_probable_direction(price, calls);

                if (force > 0 && state == 0) {
                    double cvar = evt_engine.compute_expected_shortfall(0.99);
                    double fraction = kelly_engine.compute_fraction(0.65, 1.5, 0.1);

                    // AlphaEvolve FSM 4D and Compliance Check
                    risk::Portfolio port{100000.0, 50000.0};
                    double safe_leverage = alpha_fsm.compute_safe_leverage(port.net_liquidation_value, cvar);

                    std::vector<std::string> zero_slippage_tickers = {"SPY", "QQQ", "AAPL"};
                    alpha_fsm.optimize_nd_calendar_spreads(zero_slippage_tickers, 15.0);

                    risk::StrategyType current_strat = alpha_fsm.get_active_strategy();
                    if (!risk::ComplianceGuard::is_order_safe(current_strat, 1000.0 * safe_leverage, cvar, port)) {
                        continue; // Blocked by Warren Buffett Guard
                    }

                    if (cvar < 5.0 && fraction > 0.0) {
                        core::OrderMessage master_order(1, 12345, 100, price, 0, 1, 0); // Buy 100
                        std::vector<core::OrderMessage> frags;

                        router.split_order(master_order, frags, 4); // Split into 4 chunks

                        for (const auto& frag : frags) {
                            if (frag.broker_id == 0) {
                                ibkr.place_order("SPY", "BUY", frag.quantity, frag.limit_price);
                                router.record_execution(frag.broker_id, true, 45.0); // Record success latency
                            }
                        }
                    }
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
