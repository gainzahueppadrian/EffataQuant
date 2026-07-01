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
#include "risk/law_of_large_numbers.hpp"
#include "core/advanced_data_structures.hpp"
#include "ipc/zmq_reactor.hpp"
#include "security/enclave_signer.hpp"
#include "core/qos_queue.hpp"
#include "agents/hypothesis_memory.hpp"
#include "analytics/trade_journal.hpp"

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

    // Multi-Tier QoS Execution Queue
    core::QoSQualityQueue execution_queue;

    // Security & Analytics
    security::EnclaveSigner signer;
    analytics::TradeJournal journal;
    agents::HypothesisDatabase ai_memory;

    std::thread trading_engine([&]() {
        std::vector<pricing::HamiltonianEngine::OptionData> calls = { {105.0, 2.0, 1000, 5000, 0.05} };
        std::vector<pricing::HamiltonianEngine::OptionData> puts = { {95.0, 4.0, 1500, 6000, 0.06} };

        core::IterativeSegmentTree seg_tree(1024);
        risk::StableVariance variance_tracker;
        risk::LawOfLargeNumbers lln;
        risk::CRRAKelly crra_kelly;

        while (running || !market_data_queue.empty_heuristic()) {
            reactor.poll_commands();

            Eigen::VectorXd obs;
            if (market_data_queue.pop(obs)) {
                double price = obs(0);
                variance_tracker.update(price);

                alpha_fsm.update_regime_hierarchy(0.5, false);
                if (alpha_fsm.detect_spoofing(10.0, 0.9)) {
                    continue;
                }

                evt_engine.update_online(std::abs(price - 100.0));

                int state = regime_engine.filter_online(obs);
                if (regime_engine.regime_change_detected()) {
                    regime_engine.reset_regime_change_flag();
                }


                // Anchored VWAP Evaluation (Brian Shannon methodology)
                // Simulated AVWAP values (In reality, calculated via Data Feed cumulative (Price*Volume)/Volume)
                double avwap_earnings = 100.5;
                double avwap_gap = 99.8;
                double avwap_ath = 105.0;

                bool avwap_confluence = alpha_fsm.evaluate_anchored_vwap(price, avwap_earnings, avwap_gap, avwap_ath);

                double force = pricing_engine.compute_probable_direction(price, calls);

                if (force > 0 && state == 0 && avwap_confluence) {
                    double cvar = evt_engine.compute_expected_shortfall(0.99);

                    risk::Portfolio port{100000.0, 50000.0};
                    double safe_leverage = alpha_fsm.compute_safe_leverage(port.net_liquidation_value, cvar);

                    std::vector<std::string> zero_slippage_tickers = {"SPY", "QQQ", "AAPL"};
                    alpha_fsm.optimize_nd_calendar_spreads(zero_slippage_tickers, 15.0);

                    risk::StrategyType current_strat = alpha_fsm.get_active_strategy();
                    if (!risk::ComplianceGuard::is_order_safe(current_strat, 1000.0 * safe_leverage, cvar, port)) {
                        continue;
                    }

                    double r_of_ruin = lln.calculate_risk_of_ruin(0.70, 1.5, 0.02, 1000, 100);
                    double optimal_f = crra_kelly.compute_fraction(0.70, 1.5, 2.0);

                    if (cvar < 5.0 && optimal_f > 0.0 && r_of_ruin < 0.01) {
                        core::OrderMessage master_order(1, 12345, 100, price, 0, 1, 0);

                        // Cryptographic Signature
                        master_order.signature = signer.sign_payload(&master_order, 27);

                        std::vector<core::OrderMessage> frags;
                        router.split_order(master_order, frags, 4);

                        for (const auto& frag : frags) {
                            execution_queue.enqueue(core::Priority::NORMAL, frag);
                        }
                    }
                }
            } else {
                core::cpu_relax();
            }
        }
    });

    // Execution Draining Thread
    std::thread execution_drainer([&]() {
        while (running || execution_queue.has_items()) {
            core::OrderMessage msg;
            if (execution_queue.dequeue(msg)) {
                if (signer.verify_signature(&msg, 27, msg.signature)) {
                    ibkr.place_order("SPY", "BUY", msg.quantity, msg.limit_price);
                    journal.record_trade(true, 150.0, 5000.0); // Simulated win
                } else {
                    std::cerr << "[SECURITY] INVALID ORDER SIGNATURE DETECTED!" << std::endl;
                }
            } else {
                core::cpu_relax();
            }
        }
    });
    data_feed.join();
    trading_engine.join();
    execution_drainer.join();
    journal.print_report();
    ibkr.disconnect();

    std::cout << "System shutdown gracefully." << std::endl;
    return 0;
}
