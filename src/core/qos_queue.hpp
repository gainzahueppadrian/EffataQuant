#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include "messages.hpp"

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::core {

enum class Priority {
    CRITICAL, // Hedging, Exits, Gamma exposure protections
    NORMAL,   // PMCC, Standard Vertical Spreads
    LOW       // Covered Calls, Cash Secured Puts
};

/**
 * @brief Multi-Tier QoS Order Execution Queue
 * Replaces the basic SPSC ring buffer for outgoing execution to ensure critical hedges
 * are processed instantly before low-priority trades, ensuring survival in flash crashes.
 */
class QoSQualityQueue {
public:
    QoSQualityQueue() {}

    HOT void enqueue(Priority priority, const OrderMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (priority == Priority::CRITICAL) {
            critical_queue_.push(msg);
        } else if (priority == Priority::NORMAL) {
            normal_queue_.push(msg);
        } else {
            low_queue_.push(msg);
        }
    }

    HOT bool dequeue(OrderMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!critical_queue_.empty()) {
            msg = critical_queue_.front();
            critical_queue_.pop();
            return true;
        }
        if (!normal_queue_.empty()) {
            msg = normal_queue_.front();
            normal_queue_.pop();
            return true;
        }
        if (!low_queue_.empty()) {
            msg = low_queue_.front();
            low_queue_.pop();
            return true;
        }
        return false;
    }

    bool has_items() {
        std::lock_guard<std::mutex> lock(mutex_);
        return !critical_queue_.empty() || !normal_queue_.empty() || !low_queue_.empty();
    }
private:
    // In strict sub-nanosecond HFT, these would be 3 discrete lock-free queues.
    // We use std::queue + mutex here as a lightweight structural stand-in representing
    // the prioritized draining logic.
    std::queue<OrderMessage> critical_queue_;
    std::queue<OrderMessage> normal_queue_;
    std::queue<OrderMessage> low_queue_;
    std::mutex mutex_;
};

} // namespace berkshire::core
