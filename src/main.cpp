#include <iostream>
#include <thread>
#include <cstdlib>
#include "core/ring_buffer.hpp"
#include "core/messages.hpp"
#include "regime/dgmh_eigen.hpp"
#include "pricing/hamiltonian.hpp"
#include "pricing/dp_knapsack_optimizer.hpp"
#include "ibkr/ibkr_wrapper.hpp"
#include "execution/smart_router.hpp"
#include "execution/alpha_evolve_fsm.hpp"
#include "risk/compliance_guard.hpp"
#include "risk/evt_engine.hpp"
#include "risk/law_of_large_numbers.hpp"
#include "core/advanced_data_structures.hpp"
#include "ipc/zmq_reactor.hpp"
#include "pricing/aad_greeks.hpp"
#include "analytics/ukf_tracker.hpp"
#include "risk/t_copula_cvar.hpp"
#include "risk/greeks_kelly.hpp"
#include "analytics/greeks_aggregator.hpp"
#include "analytics/asymmetric_engine.hpp"
#include "optimization/hrp_allocator.hpp"
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
    pricing::DPKnapsackOptimizer dp_optimizer;


    // 4. Execution & Routing
    ibkr::IBKRWrapper ibkr("127.0.0.1", 7497, 1);
    execution::SmartOrderRouter router(3); // 3 Brokers (e.g., IBKR, Lightspeed, Schwab)
    execution::AlphaEvolveFSM alpha_fsm;
    // Advanced Econophysics Models
    analytics::UnscentedKalmanFilter ukf(2, 1);
    risk::TCopulaCVaR t_copula(3, 4.0); // 3 assets, 4.0 degrees of freedom (fat tails)


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


                // AAD $O(1)$ Greeks Engine Evaluation
                // Replaces slow Finite Differences

                // Hamiltonian Econophysics Engine Evaluation
                // Bypasses Black-Scholes completely using Liquidity Gravity Fields and UKF Volatility
                pricing::AADResult greeks = pricing::AADEngine::compute_hamiltonian(
                    price, 100.0, 30.0/365.0, 0.20, 5000.0, 15000.0, true);


                // Track Implied Volatility non-linearities via Unscented Kalman Filter
                Eigen::VectorXd iv_obs(1);
                iv_obs << 0.20; // Dummy IV
                ukf.predict();
                ukf.update(iv_obs);

                double force = pricing_engine.compute_probable_direction(price, calls);

                // AVWAP
                double avwap_earnings = 100.5;
                double avwap_gap = 99.8;
                double avwap_ath = 105.0;
                bool avwap_confluence = alpha_fsm.evaluate_anchored_vwap(price, avwap_earnings, avwap_gap, avwap_ath);

                if (force > 0 && state == 0 && avwap_confluence) {
                    double cvar = evt_engine.compute_expected_shortfall(0.99);

                    risk::Portfolio port{100000.0, 50000.0};
                    double safe_leverage = alpha_fsm.compute_safe_leverage(port.net_liquidation_value, cvar);

                    // DP Knapsack optimization for Structure Generation (now with 2nd-order greeks)
                    std::vector<pricing::OptionLeg> universe = {
                        // symbol, strike, dte, is_call, price, delta, gamma, theta, vega, vanna, charm, vomma, veta, speed, zomma, color, ultima, iv
                        {"SPY", 100.0, 30, true, 2.50, 0.45, 0.08, -0.05, 0.12, 0.01, 0.002, 0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 0.20, 150.0},
                        {"SPY", 105.0, 7, false, 1.20, -0.30, -0.06, 0.08, -0.09, -0.01, -0.001, -0.02, 0.0, 0.0, 0.0, 0.0, 0.0, 0.22, 50.0},
                        {"SPY", 95.0, 30, false, 2.80, -0.45, 0.08, -0.04, 0.14, 0.02, 0.003, 0.04, 0.0, 0.0, 0.0, 0.0, 0.0, 0.25, 200.0},
                        {"SPY", 90.0, 7, true, 1.00, 0.30, -0.05, 0.07, -0.10, -0.02, -0.002, -0.01, 0.0, 0.0, 0.0, 0.0, 0.0, 0.28, 40.0}
                    };

                    pricing::KnapsackConfig dp_config;
                    dp_config.regime = pricing::MarketRegime::VOLATILITY_CONTRACTION;
                    dp_config.target_portfolio_delta = 0.0;
                    dp_config.max_portfolio_cvar = port.net_liquidation_value * safe_leverage;
                    dp_config.required_legs = 4;

                    auto optimized_structure = dp_optimizer.solve(dp_config, universe);

                    if (optimized_structure.has_value()) {
                        std::cout << "[Executor] ✅ 4-Leg Double Diagonal structure assembled successfully via DP Knapsack." << std::endl;

                        // Step 8: Registrar P&L attribution por Greeks
                        auto agg = analytics::GreeksAggregator::aggregate_portfolio(optimized_structure.value().selected_legs, price);
                        analytics::GreeksAggregator::print_attribution(agg);

                        // Asymmetric Strategy Analysis based on the central position (Master Order)
                        auto gs_metrics = analytics::AsymmetricStrategyEngine::analyze_gamma_scalping(greeks, price, 0.80, 0.20);
                        auto vc_metrics = analytics::AsymmetricStrategyEngine::analyze_volatility_convexity(greeks, 0.50);
                        auto ch_metrics = analytics::AsymmetricStrategyEngine::analyze_charm_unwind(greeks, 1.0);

                        std::cout << "\n💰 ANÁLISIS DE ESTRATEGIA ASIMÉTRICA:" << std::endl;
                        std::cout << "   1. GAMMA SCALPING POTENTIAL:" << std::endl;
                        std::cout << "      Edge Ratio: " << gs_metrics.edge_ratio << "x" << std::endl;
                        std::cout << "   2. EXPOSICIÓN A CONVEXIDAD DE VOLATILIDAD:" << std::endl;
                        std::cout << "      Beneficio de Convexidad: $" << vc_metrics.convexity_benefit << " (" << vc_metrics.exposure_type << ")" << std::endl;
                        std::cout << "   3. PERFIL DE DESARME POR CHARM:" << std::endl;
                        std::cout << "      Flujo MM por contrato: " << ch_metrics.mm_hedge_flow_per_contract << " acciones" << std::endl;
                        std::cout << "      Dirección: " << ch_metrics.flow_direction << " | Intensidad: " << ch_metrics.intensity << std::endl;
                    }

                    // Evaluate Lateral vs Directional Regimes for Double Diagonal structures
                    if (state == 0) {
                        alpha_fsm.orchestrate_double_diagonal(state, 0.0);
                    } else if (state == 1) {
                        alpha_fsm.orchestrate_double_diagonal(state, force);
                    }

                    risk::StrategyType current_strat = alpha_fsm.get_active_strategy();
                    if (!risk::ComplianceGuard::is_order_safe(current_strat, 1000.0 * safe_leverage, cvar, port)) {
                        continue; // Blocked by Warren Buffett Guard
                    }

                    double r_of_ruin = lln.calculate_risk_of_ruin(0.70, 1.5, 0.02, 1000, 100);
                    double base_f = risk::GreeksKelly::compute_base_kelly(0.70, 1.5);
                    double optimal_f = risk::GreeksKelly::compute_enhanced_kelly(base_f, greeks, force, 0.20);

                    if (cvar < 5.0 && optimal_f > 0.0 && r_of_ruin < 0.01) {
                        core::OrderMessage master_order(1, 12345, 100, price, 0, 1, 0);
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
    });    // Execution Draining Thread
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
