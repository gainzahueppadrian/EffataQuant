#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace berkshire::agents {

struct Hypothesis {
    int id;
    std::string description;
    double profit_loss;
    std::chrono::system_clock::time_point timestamp;

    double decay_weight() const {
        auto now = std::chrono::system_clock::now();
        auto seconds_diff = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count();
        double days_diff = seconds_diff / 86400.0;
        // Exponential decay function: exp(-0.1 * delta_t)
        return std::exp(-0.1 * days_diff);
    }

    double score() const {
        return profit_loss * decay_weight();
    }
};

/**
 * @brief Adversarial Hypothesis Memory with Exponential Decay
 * Integrates Co-Scientist generated ideas, tracking their decay over time so the
 * multi-agent orchestrator forgets obsolete market assumptions.
 */
class HypothesisDatabase {
public:
    void add_hypothesis(int id, const std::string& desc, double pnl) {
        hypotheses_.push_back({id, desc, pnl, std::chrono::system_clock::now()});
    }

    /**
     * @brief Retrieve Top K hypotheses actively ranked by decaying PnL.
     */
    std::vector<Hypothesis> get_top_k(size_t k) {
        std::vector<Hypothesis> sorted = hypotheses_;
        std::sort(sorted.begin(), sorted.end(), [](const Hypothesis& a, const Hypothesis& b) {
            return a.score() > b.score();
        });

        if (sorted.size() > k) {
            sorted.resize(k);
        }
        return sorted;
    }

private:
    std::vector<Hypothesis> hypotheses_;
};

} // namespace berkshire::agents
