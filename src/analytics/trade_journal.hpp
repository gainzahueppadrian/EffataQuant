#pragma once

#include <atomic>
#include <iostream>

#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::analytics {

/**
 * @brief TradeZella Clone - Professional HFT Trading Journal Analytics
 * Audits system performance continually without locking the main thread.
 */
class TradeJournal {
public:
    ALWAYS_INLINE void record_trade(bool is_win, double pnl, double margin_used) {
        total_trades_.fetch_add(1, std::memory_order_relaxed);
        cumulative_pnl_.fetch_add(pnl, std::memory_order_relaxed);

        if (is_win) {
            winning_trades_.fetch_add(1, std::memory_order_relaxed);
            gross_profit_.fetch_add(pnl, std::memory_order_relaxed);
        } else {
            losing_trades_.fetch_add(1, std::memory_order_relaxed);
            gross_loss_.fetch_add(std::abs(pnl), std::memory_order_relaxed);
        }

        double current_max_margin = max_margin_used_.load(std::memory_order_relaxed);
        if (margin_used > current_max_margin) {
            max_margin_used_.store(margin_used, std::memory_order_relaxed);
        }
    }

    double get_win_rate() const {
        double total = total_trades_.load(std::memory_order_relaxed);
        if (total == 0) return 0.0;
        return (winning_trades_.load(std::memory_order_relaxed) / total) * 100.0;
    }

    double get_profit_factor() const {
        double gl = gross_loss_.load(std::memory_order_relaxed);
        if (gl == 0) return gross_profit_.load(std::memory_order_relaxed); // Infinite
        return gross_profit_.load(std::memory_order_relaxed) / gl;
    }

    double get_roic() const {
        double margin = max_margin_used_.load(std::memory_order_relaxed);
        if (margin == 0) return 0.0;
        return (cumulative_pnl_.load(std::memory_order_relaxed) / margin) * 100.0;
    }

    void print_report() const {
        std::cout << "\n===== TRADEZELLA CLONE METRICS =====" << std::endl;
        std::cout << "Total Trades: " << total_trades_.load() << std::endl;
        std::cout << "Win Rate: " << get_win_rate() << "%" << std::endl;
        std::cout << "Profit Factor: " << get_profit_factor() << std::endl;
        std::cout << "Cumulative PnL: $" << cumulative_pnl_.load() << std::endl;
        std::cout << "ROIC on Max Margin: " << get_roic() << "%" << std::endl;
        std::cout << "====================================\n" << std::endl;
    }

private:
    std::atomic<uint64_t> total_trades_{0};
    std::atomic<uint64_t> winning_trades_{0};
    std::atomic<uint64_t> losing_trades_{0};

    std::atomic<double> cumulative_pnl_{0.0};
    std::atomic<double> gross_profit_{0.0};
    std::atomic<double> gross_loss_{0.0};
    std::atomic<double> max_margin_used_{0.0};
};

} // namespace berkshire::analytics
