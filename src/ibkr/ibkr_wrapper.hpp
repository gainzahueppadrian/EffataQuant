#pragma once

#include <iostream>
#include <string>
#include <atomic>
#include <chrono>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::ibkr {

/**
 * @brief Ultra-low latency IBKR execution wrapper dummy implementation.
 * In a real-world scenario, this connects via POSIX sockets or directly uses
 * the Interactive Brokers TWS C++ API for sub-nanosecond/microsecond order routing.
 */
class IBKRWrapper {
public:
    IBKRWrapper(const std::string& host, int port, int client_id)
        : host_(host), port_(port), client_id_(client_id), is_connected_(false) {}

    bool connect() {
        // Simulate connection
        is_connected_.store(true, std::memory_order_release);
        std::cout << "[IBKR] Connected to " << host_ << ":" << port_ << std::endl;
        return true;
    }

    void disconnect() {
        is_connected_.store(false, std::memory_order_release);
        std::cout << "[IBKR] Disconnected." << std::endl;
    }

    /**
     * @brief Send an order bypassing GC and overhead, straight to the socket.
     */
    HOT ALWAYS_INLINE bool place_order(const std::string& symbol, const std::string& action, int quantity, double price) {
        if (!is_connected_.load(std::memory_order_acquire)) {
            return false;
        }

        auto start = std::chrono::high_resolution_clock::now();

        // --- SIMULATED SOCKET WRITE ---
        // write(socket_fd, buffer, length);

        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

        std::cout << "[IBKR] " << action << " " << quantity << " " << symbol
                  << " @ " << price << " | Latency: " << latency << " ns" << std::endl;

        return true;
    }

private:
    std::string host_;
    int port_;
    int client_id_;
    std::atomic<bool> is_connected_;
};

} // namespace berkshire::ibkr
