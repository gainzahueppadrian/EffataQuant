#pragma once

#include <atomic>
#include <vector>
#include <utility>
#include <cstdint>
#include <stdexcept>
#include <optional>
#include <new>

#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h> // For _mm_pause
#endif

namespace berkshire::core {

// Prevent false sharing by aligning to 64 bytes (typical cache line size)
#if defined(__cpp_lib_hardware_interference_size)
    constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr size_t CACHE_LINE_SIZE = 64;
#endif

inline void cpu_relax() {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#else
    // Fallback
#endif
}

/**
 * @brief Lock-free Single-Producer Single-Consumer (SPSC) Ring Buffer
 * Inspired by LMAX Disruptor for sub-nanosecond latency event passing.
 */
template <typename T, size_t Size>
class alignas(CACHE_LINE_SIZE) RingBuffer {
    static_assert((Size != 0) && ((Size & (Size - 1)) == 0), "Size must be a power of 2");

public:
    RingBuffer() : buffer_(Size) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    /**
     * @brief Pushes an item to the ring buffer.
     * @param item The item to push.
     * @return true if successful, false if buffer is full.
     */
    template <typename U>
    bool push(U&& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next_head = (head + 1) & mask_;

        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }

        buffer_[head] = std::forward<U>(item);
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops an item from the ring buffer.
     * @param item Reference to store the popped item.
     * @return true if successful, false if buffer is empty.
     */
    bool pop(T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }

        item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    /**
     * @brief Blocking pop that spins until an item is available.
     * HFT use case: burn CPU cycles to get minimal latency.
     */
    void pop_spin(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        while (tail == head_.load(std::memory_order_acquire)) {
            cpu_relax();
        }
        item = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
    }

    bool empty_heuristic() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

private:
    std::vector<T> buffer_;
    static constexpr size_t mask_ = Size - 1;

    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
};

} // namespace berkshire::core
