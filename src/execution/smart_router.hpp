#pragma once

#include <vector>
#include <random>
#include <cmath>
#include <Eigen/Dense>

#include <immintrin.h>
#include "../core/messages.hpp"

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::execution {

/**
 * @brief Smart Order Router using Multi-Armed Bandits (Thompson Sampling)
 * Learns optimal broker routing by modeling latency as a Gamma distribution.
 */
class SmartOrderRouter {
public:
    struct BrokerStats {
        double alpha = 1.0; // Shape parameter (successes/speed)
        double beta = 1.0;  // Rate parameter (failures/slowness)

        // Update beliefs (Bayesian update)
        ALWAYS_INLINE void update(bool success, double latency_penalty) {
            if (success) {
                alpha += 1.0;
            } else {
                beta += latency_penalty;
            }
        }
    };

    SmartOrderRouter(int num_brokers) : brokers_(num_brokers), rng_(std::random_device{}()) {}

    /**
     * @brief Thompson Sampling to select the best broker.
     * Draws a sample from the Gamma distribution of each broker and picks the highest.
     */
    HOT uint8_t select_best_broker() {
        uint8_t best_broker = 0;
        double max_sample = -1.0;

        for (size_t i = 0; i < brokers_.size(); ++i) {
            std::gamma_distribution<double> gamma(brokers_[i].alpha, 1.0 / brokers_[i].beta);
            double sample = gamma(rng_);
            if (sample > max_sample) {
                max_sample = sample;
                best_broker = static_cast<uint8_t>(i);
            }
        }
        return best_broker;
    }

    /**
     * @brief Split a large order into fragments to avoid market impact and Dark Pool detection.
     */
    HOT void split_order(const core::OrderMessage& large_order, std::vector<core::OrderMessage>& out_fragments, int num_fragments) {
        uint32_t qty_per_frag = large_order.quantity / num_fragments;
        uint32_t remainder = large_order.quantity % num_fragments;

        for (int i = 0; i < num_fragments; ++i) {
            core::OrderMessage fragment = large_order;
            fragment.quantity = qty_per_frag + (i == 0 ? remainder : 0);

            // Re-route dynamically using Thompson Sampling
            fragment.broker_id = select_best_broker();
            out_fragments.push_back(fragment);
        }
    }

    // Feedback loop for the agent to learn from execution
    void record_execution(uint8_t broker_id, bool success, double latency_ms) {
        brokers_[broker_id].update(success, latency_ms);
    }

private:
    std::vector<BrokerStats> brokers_;
    std::mt19937 rng_;
};

} // namespace berkshire::execution