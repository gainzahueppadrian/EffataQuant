#pragma once
#include <atomic>
#include <chrono>
#include <array>

#define HOT [[gnu::hot]]

namespace effata::risk {

/**
 * Detecta y bloquea automáticamente el "Revenge Trading"
 * Optimizado para sub-nanosecond HFT con Ring Buffer Lock-Free
 */
class AntiRevengeCircuitBreaker {
private:
    struct TradeRecord {
        std::chrono::nanoseconds timestamp;
        double pnl;
        double position_size_multiplier;
    };

    // Lock-Free Ring Buffer instead of std::vector
    static constexpr size_t RING_SIZE = 128;
    std::array<TradeRecord, RING_SIZE> recent_trades_{};
    std::atomic<size_t> write_index_{0};

    static constexpr int MAX_TRADES_IN_WINDOW = 5;
    static constexpr std::chrono::minutes TIME_WINDOW = std::chrono::minutes(15);
    static constexpr double MAX_LOSS_STREAK_MULTIPLIER = 2.0;

    std::atomic<bool> is_circuit_broken_{false};
    std::atomic<int64_t> break_until_ns_{0};

    // Spinlock para evitar std::mutex blocking
    std::atomic_flag spinlock_ = ATOMIC_FLAG_INIT;

    void lock() {
        while (spinlock_.test_and_set(std::memory_order_acquire)) {
            // spin (en HFT usar _mm_pause() o sched_yield() es comun)
#if defined(__x86_64__)
            __asm__ __volatile__("pause");
#endif
        }
    }

    void unlock() {
        spinlock_.clear(std::memory_order_release);
    }

public:
    HOT void record_trade(double pnl, double size_multiplier) {
        auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch());

        // Use a lightweight spinlock to protect ring buffer updates
        lock();

        size_t idx = write_index_.load(std::memory_order_relaxed) % RING_SIZE;
        recent_trades_[idx] = {now, pnl, size_multiplier};
        write_index_.fetch_add(1, std::memory_order_relaxed);

        evaluate_behavior(now);
        unlock();
    }

    [[nodiscard]] inline bool is_allowed_to_trade() const {
        if (is_circuit_broken_.load(std::memory_order_relaxed)) {
            auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (now > break_until_ns_.load(std::memory_order_relaxed)) {
                is_circuit_broken_.store(false, std::memory_order_relaxed);
                return true;
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] std::string get_status_message() const {
        if (is_circuit_broken_.load(std::memory_order_relaxed)) {
            return "🚨 CIRCUIT BREAKER ACTIVO: Comportamiento de venganza detectado. Sistema bloqueado por 2 horas.";
        }
        return "✅ Sistema operativo. Gestión de riesgo normal.";
    }

private:
    HOT void evaluate_behavior(std::chrono::nanoseconds current_time) {
        size_t current_count = write_index_.load(std::memory_order_relaxed);
        if (current_count < MAX_TRADES_IN_WINDOW) return;

        auto cutoff = current_time - std::chrono::nanoseconds(TIME_WINDOW.count() * 60'000'000'000ULL);

        int consecutive_losses = 0;
        bool size_escalation_after_loss = false;
        double total_window_pnl = 0.0;

        size_t trades_eval = std::min(current_count, RING_SIZE);

        double last_loss_multiplier = 0.0;
        bool was_last_loss = false;

        // Iterate backwards from newest to oldest
        for (size_t i = 0; i < trades_eval; ++i) {
            size_t idx = (current_count - 1 - i) % RING_SIZE;
            const auto& trade = recent_trades_[idx];

            if (trade.timestamp < cutoff) break; // Out of window

            total_window_pnl += trade.pnl;

            if (trade.pnl < 0) {
                consecutive_losses++;
                if (was_last_loss) {
                     // Check if previous (newer) trade escalated size relative to this one
                     if (last_loss_multiplier > trade.position_size_multiplier * MAX_LOSS_STREAK_MULTIPLIER) {
                         size_escalation_after_loss = true;
                     }
                }
                last_loss_multiplier = trade.position_size_multiplier;
                was_last_loss = true;
            } else {
                was_last_loss = false;
            }
        }

        // Activation Logic
        if (consecutive_losses >= 3 || size_escalation_after_loss || total_window_pnl < -0.05) { // 5% pérdida en 15 min
            is_circuit_broken_.store(true, std::memory_order_relaxed);
            break_until_ns_.store(current_time.count() + (2 * 3600 * 1'000'000'000ULL), std::memory_order_relaxed); // 2 horas
        }
    }
};

} // namespace effata::risk
