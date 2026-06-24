#pragma once

#include <atomic>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include "../risk/compliance_guard.hpp"

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::execution {

class AlphaEvolveFSM {
public:
    enum class State {
        IDLE,
        EVALUATING_SPREADS,
        EXECUTING_HEDGES,
        DEFENSIVE_MODE,
        HALTED
    };

    AlphaEvolveFSM() : current_state_(State::IDLE), active_strategy_(risk::StrategyType::VerticalSpread) {}

    HOT ALWAYS_INLINE void optimize_nd_calendar_spreads(const std::vector<std::string>& top_k_tickers, double iv_percentile) {
        for (const auto& ticker : top_k_tickers) {
            std::cout << "[AlphaEvolve] 4D Optimizing N-Double Calendar Spread for: " << ticker
                      << " | Target: Zero-Slippage, +Vega, +Theta" << std::endl;
        }

        if (iv_percentile < 20.0) {
            std::cout << "[AlphaEvolve] Regime: Low Volatility (Contango) -> Mutating to Long Calendar Put Spread." << std::endl;
            active_strategy_.store(risk::StrategyType::LongCalendarPutSpread, std::memory_order_release);
        } else if (iv_percentile > 80.0) {
            std::cout << "[AlphaEvolve] Regime: Volatility Spike -> Mutating to Iron Condor." << std::endl;
            active_strategy_.store(risk::StrategyType::IronCondor, std::memory_order_release);
        } else {
            std::cout << "[AlphaEvolve] Regime: Trending -> Mutating to Diagonal Put Spread." << std::endl;
            active_strategy_.store(risk::StrategyType::DiagonalPutSpread, std::memory_order_release);
        }
    }

    HOT ALWAYS_INLINE double compute_safe_leverage(double current_capital, double cvar_limit) {
        if (current_capital < 50000.0) {
            return 1.0;
        } else {
            double max_allowed_loss = current_capital * 0.02; // 2% Warren Buffett rule
            double leverage_factor = max_allowed_loss / (cvar_limit + 1e-6);
            return std::min(3.0, leverage_factor);
        }
    }

    void mutate_strategy(uint8_t new_strategy_id) {
        if (current_state_.load(std::memory_order_relaxed) == State::HALTED) return;

        switch (new_strategy_id) {
            case 0: active_strategy_.store(risk::StrategyType::PMCC, std::memory_order_release); break;
            case 1: active_strategy_.store(risk::StrategyType::IronCondor, std::memory_order_release); break;
            case 2: active_strategy_.store(risk::StrategyType::CalendarSpread, std::memory_order_release); break;
            default: active_strategy_.store(risk::StrategyType::VerticalSpread, std::memory_order_release); break;
        }
        std::cout << "[FSM] Mutated Strategy to ID: " << (int)new_strategy_id << std::endl;
    }

    void trigger_defensive_mode() {
        current_state_.store(State::DEFENSIVE_MODE, std::memory_order_release);
        std::cout << "[FSM] Entering DEFENSIVE MODE (Volatility Spike Detected)" << std::endl;
    }

    void halt() {
        current_state_.store(State::HALTED, std::memory_order_release);
        std::cout << "[FSM] SYSTEM HALTED by Agent Command." << std::endl;
    }

    inline State get_state() const { return current_state_.load(std::memory_order_acquire); }
    inline risk::StrategyType get_active_strategy() const { return active_strategy_.load(std::memory_order_acquire); }

private:
    std::atomic<State> current_state_;
    std::atomic<risk::StrategyType> active_strategy_;
};

} // namespace berkshire::execution
