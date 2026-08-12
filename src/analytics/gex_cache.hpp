#pragma once
#include <vector>
#include <span>
#include <cmath>
#include "../core/quantum_data.hpp"
#include "../pricing/bates_pricer.hpp"

namespace effata::analytics {

using namespace effata::core;

class GEXCache {
public:
    struct Entry { double spot; double gamma; uint64_t last_tick; };

    void update_chain(std::span<const MarketData> chain, double spot,
                      uint64_t tick, const ModelParams& p, double v0) {
        constexpr double INVALIDATION_REL = 0.005;   // Re-price si spot mueve >0.5%
        if (entries_.size() != chain.size()) {
            entries_.resize(chain.size());
            dirty_ = true;
        }
        bool need_reprice = dirty_ ||
            std::abs(spot - last_spot_) / last_spot_ > INVALIDATION_REL;

        if (need_reprice) {
            pricing::BatesPricer pricer;
            for (size_t i = 0; i < chain.size(); ++i) {
                double T = chain[i].expiry_days / 365.25;
                if (T <= 0.0) T = 1.0 / 365.25;

                double gamma_val = pricer.gamma(spot, chain[i].strike, T, v0, p);
                entries_[i] = {spot, gamma_val, tick};
            }
            last_spot_ = spot;
            dirty_ = false;
        }
    }

    struct Result { double net_gex; double flip_zone; };
    Result compute(double spot, std::span<const MarketData> chain) const {
        if (entries_.empty() || chain.empty()) return {0.0, spot};

        double call_gex = 0, put_gex = 0, cum = 0;
        double flip = spot;
        bool found = false;
        for (size_t i = 0; i < chain.size(); ++i) {
            double g = entries_[i].gamma * chain[i].oi * 100.0 * spot * spot;
            double sign = chain[i].is_call() ? 1.0 : -1.0;
            if (chain[i].is_call()) call_gex += g; else put_gex += g;
            cum += sign * g;
            if (cum < 0 && !found) { flip = chain[i].strike; found = true; }
        }
        return {call_gex + put_gex, flip};
    }

private:
    std::vector<Entry> entries_;
    double last_spot_ = 0.0;
    bool dirty_ = true;
};

} // namespace effata::analytics
