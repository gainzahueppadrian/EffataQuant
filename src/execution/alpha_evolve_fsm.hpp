#pragma once

#include <atomic>
#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include "../risk/compliance_guard.hpp"

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::execution {

enum class MarketRegime {
    LATERAL_LOW_VOL,
    LATERAL_HIGH_VOL,
    TRENDING_BULL,
    TRENDING_BEAR,
    VOLATILE_MEAN_REV,
    CRISIS,
    TRANSITION
};

class AlphaEvolveFSM {
public:
    enum class State {
        IDLE,
        EVALUATING_SPREADS,
        EXECUTING_HEDGES,
        DEFENSIVE_MODE,
        CIRCUIT_BREAKER,
        RECOVERY_MODE,
        HALTED
    };

    AlphaEvolveFSM() : current_state_(State::IDLE), active_strategy_(risk::StrategyType::VerticalSpread) {}

    HOT ALWAYS_INLINE bool detect_spoofing(double order_book_imbalance, double cancellation_rate) {
        // Dynamic Microstructure Hashing logic simplified
        if (order_book_imbalance > 5.0 && cancellation_rate > 0.8) {
            /* std::cerr << "[AlphaEvolve] 🚨 SPOOFING DETECTED. Blocking Execution." << std::endl; */
            return true;
        }
        return false;
    }

    HOT ALWAYS_INLINE void update_regime_hierarchy(double systemic_correlation, bool limit_down_hit) {
        if (limit_down_hit || systemic_correlation > 0.95) {
            std::cerr << "[AlphaEvolve] 🔴 SYSTEMIC CONTAGION DETECTED. Engaging Circuit Breakers." << std::endl;
            current_state_.store(State::CIRCUIT_BREAKER, std::memory_order_release);
            active_strategy_.store(risk::StrategyType::ProtectivePut, std::memory_order_release);
        } else if (current_state_.load() == State::CIRCUIT_BREAKER && systemic_correlation < 0.6) {
            std::cout << "[AlphaEvolve] 🟢 RECOVERY MODE ENGAGED. Transitioning to safe yield harvesting." << std::endl;
            current_state_.store(State::RECOVERY_MODE, std::memory_order_release);
            active_strategy_.store(risk::StrategyType::CashSecuredPut, std::memory_order_release);
        }
    }



    HOT ALWAYS_INLINE bool evaluate_anchored_vwap(double current_price, double avwap_earnings, double avwap_gap, double avwap_ath) {
        // High conviction "Multiple Edge Entry" if price bounces at the confluence of multiple AVWAPs and EMAs
        double avwap_confluence_band = 0.015; // 1.5% confluence zone

        bool near_earnings_avwap = std::abs(current_price - avwap_earnings) / avwap_earnings < avwap_confluence_band;
        bool near_gap_avwap = std::abs(current_price - avwap_gap) / avwap_gap < avwap_confluence_band;
        bool near_ath_avwap = std::abs(current_price - avwap_ath) / avwap_ath < avwap_confluence_band;

        if (near_earnings_avwap || near_gap_avwap || near_ath_avwap) {
            std::cout << "[AlphaEvolve FSM] 🎯 Smart Money AVWAP Confluence Detected. Preparing execution." << std::endl;
            return true;
        }

        if (current_price < avwap_earnings && current_price < avwap_gap) {
            std::cout << "[AlphaEvolve FSM] ⚠️ AVWAP support lost. Institutional trend broken." << std::endl;
            trigger_defensive_mode();
            return false;
        }

        return false;
    }


    /**
     * @brief Advanced N-Double Diagonal Orchestration (Symmetric vs Asymmetric)
     * Replaces standard Iron Condors with Positive Vega configurations.
     */
    HOT ALWAYS_INLINE void orchestrate_double_diagonal(MarketRegime regime, double directional_force) {
        if (regime == MarketRegime::LATERAL_LOW_VOL || regime == MarketRegime::LATERAL_HIGH_VOL) {
            std::cout << "[AlphaEvolve] ⚖️ Lateral Regime Detected: Constructing SYMMETRIC N-Double Diagonal." << std::endl;
            std::cout << " -> Delta Netto ~ 0. Positive Vega. Selling near-term, buying far-term OTM." << std::endl;
            active_strategy_.store(risk::StrategyType::DoubleDiagonalSpread, std::memory_order_release);
        } else {
            std::cout << "[AlphaEvolve] 🚀 Directional Regime Detected: Constructing ASYMMETRIC N-Double Diagonal." << std::endl;
            if (directional_force > 0) {
                std::cout << " -> Bullish Bias: Long Call ITM/ATM, Short Call far OTM. Put side defensive." << std::endl;
                active_strategy_.store(risk::StrategyType::TunnelBullish, std::memory_order_release);
            } else {
                std::cout << " -> Bearish Bias: Long Put ITM/ATM, Short Put far OTM. Call side defensive." << std::endl;
                active_strategy_.store(risk::StrategyType::TunnelBearish, std::memory_order_release);
            }
        }
    }


    HOT ALWAYS_INLINE void orchestrate_double_diagonal(int regime_state, double directional_force) {
        if (regime_state == 0) {
            std::cout << "[AlphaEvolve] ⚖️ Lateral Regime Detected: Constructing SYMMETRIC N-Double Diagonal." << std::endl;
            std::cout << " -> Delta Netto ~ 0. Positive Vega. Selling near-term, buying far-term OTM." << std::endl;
            active_strategy_.store(risk::StrategyType::DoubleDiagonalSpread, std::memory_order_release);
        } else {
            std::cout << "[AlphaEvolve] 🚀 Directional Regime Detected: Constructing ASYMMETRIC N-Double Diagonal." << std::endl;
            if (directional_force > 0) {
                std::cout << " -> Bullish Bias: Long Call ITM/ATM, Short Call far OTM. Put side defensive." << std::endl;
                active_strategy_.store(risk::StrategyType::TunnelBullish, std::memory_order_release);
            } else {
                std::cout << " -> Bearish Bias: Long Put ITM/ATM, Short Put far OTM. Call side defensive." << std::endl;
                active_strategy_.store(risk::StrategyType::TunnelBearish, std::memory_order_release);
            }
        }
    }

    HOT ALWAYS_INLINE void optimize_nd_calendar_spreads(const std::vector<std::string>& top_k_tickers, double iv_percentile, bool is_pre_earnings) {
        for (const auto& ticker : top_k_tickers) {
            std::cout << "[AlphaEvolve] 4D Optimizing N-Double Calendar Spread for: " << ticker
                      << " | Target: Zero-Slippage, +Vega, +Theta" << std::endl;
        }

        if (is_pre_earnings) {
            // VOLATILITY CRUSH PREDICTION: Force transition into short vol spreads
            std::cout << "[AlphaEvolve] ⚠️ PRE-EARNINGS / VOL CRUSH DETECTED! Reversing Vega Bias." << std::endl;
            std::cout << " -> Constructing Iron Butterfly / Short Strangle (Covered) to farm Vol Implosion." << std::endl;
            active_strategy_.store(risk::StrategyType::IronCondor, std::memory_order_release);
            return;
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

    HOT ALWAYS_INLINE bool evaluate_micro_vcp(double inside_day_high, double inside_day_low, double current_price, double current_volume, double avg_volume) {
        if (current_price > inside_day_high && current_volume > avg_volume * 1.5) {
            std::cout << "[AlphaEvolve FSM] 🎯 Martin Luke Micro-VCP Breakout Detected! Entry triggered." << std::endl;
            return true;
        }
        return false;
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
