// effata_hft/core/cache_aligned_lock_free_queue.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <array>
#include <concepts>

namespace effata::hft {

// Garantiza que cada celda ocupe exactamente una línea de caché (64 bytes en x86/ARM)
// para evitar "False Sharing" entre hilos de producción y consumo.
template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template <TriviallyCopyable T, size_t Capacity>
requires (Capacity > 0 && (Capacity & (Capacity - 1)) == 0) // Potencia de 2 para bitwise AND
class alignas(64) CacheAlignedLockFreeQueue {

    struct alignas(64) Cell {
        std::atomic<uint64_t> sequence;
        T data;
    };

    // Separamos head y tail en líneas de caché distintas para que el productor
    // y el consumidor no invaliden la caché del otro.
    alignas(64) std::atomic<uint64_t> head_{0};
    alignas(64) std::atomic<uint64_t> tail_{0};

    std::array<Cell, Capacity> buffer_;
    static constexpr uint64_t MASK = Capacity - 1;

public:
    CacheAlignedLockFreeQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // Productor (Hilo de Red / NIC)
    [[gnu::always_inline]] bool try_push(const T& item) noexcept {
        uint64_t pos = head_.load(std::memory_order_relaxed);
        Cell& cell = buffer_[pos & MASK];
        uint64_t seq = cell.sequence.load(std::memory_order_acquire);

        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        if (diff < 0) [[unlikely]] {
            return false; // Cola llena
        }

        cell.data = item;
        // Liberamos la secuencia para que el consumidor la vea
        cell.sequence.store(pos + 1, std::memory_order_release);
        head_.store(pos + 1, std::memory_order_relaxed);
        return true;
    }

    // Consumidor (Hilo de Estrategia / Matching)
    [[gnu::always_inline]] bool try_pop(T& item) noexcept {
        uint64_t pos = tail_.load(std::memory_order_relaxed);
        Cell& cell = buffer_[pos & MASK];
        uint64_t seq = cell.sequence.load(std::memory_order_acquire);

        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        if (diff < 0) [[unlikely]] {
            return false; // Cola vacía
        }

        item = cell.data;
        // Liberamos la celda para que el productor la reutilice
        cell.sequence.store(pos + Capacity, std::memory_order_release);
        tail_.store(pos + 1, std::memory_order_relaxed);
        return true;
    }
};

} // namespace effata::hft
