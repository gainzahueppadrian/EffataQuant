// effata_hft/memory/lock_free_slab.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <new>
#include "canary_guard.hpp"

namespace effata::hft::memory {

/**
 * LockFreeSlab: Asignador de bloques de tamaño fijo, lock-free.
 *
 * Usa una free-list basada en índices (no punteros) para evitar el ABA problem.
 * Cada bloque tiene un "tag" de generación que se incrementa en cada free.
 *
 * Complejidad:
 *   - allocate: O(1) amortizado, lock-free
 *   - deallocate: O(1), lock-free
 *   - overhead memoria: 8 bytes por bloque (para el next pointer)
 */
class LockFreeSlab {
public:
    static constexpr uint64_t NULL_INDEX = std::numeric_limits<uint64_t>::max();

    struct alignas(64) Metrics {
        std::atomic<uint64_t> allocations{0};
        std::atomic<uint64_t> deallocations{0};
        std::atomic<uint64_t> peak_in_use{0};
        std::atomic<uint64_t> current_in_use{0};
        std::atomic<uint64_t> failed_allocations{0};
    };

    LockFreeSlab(void* base_memory, size_t block_size, size_t num_blocks)
        : base_(static_cast<char*>(base_memory)),
          block_size_(block_size),
          num_blocks_(num_blocks) {

        // Validaciones defensivas (Buffett Rule #1)
        if (base_ == nullptr || num_blocks == 0 || block_size < 8) {
            throw std::invalid_argument("LockFreeSlab: parámetros inválidos");
        }

        if (block_size % 8 != 0) {
            throw std::invalid_argument("LockFreeSlab: block_size debe ser múltiplo de 8");
        }

        // Inicializar la free-list como una lista enlazada de índices
        // El formato de cada slot: [NEXT_INDEX (8B)][USER DATA (block_size-8 B)]
        for (size_t i = 0; i < num_blocks_; ++i) {
            uint64_t* slot = slot_ptr(i);
            *slot = (i + 1 < num_blocks_) ? static_cast<uint64_t>(i + 1) : NULL_INDEX;
        }

        // Head apunta al primer bloque libre
        head_.store(pack(0, 0), std::memory_order_release);
    }

    // No copiable, no movible (la memoria es fija en NUMA node)
    LockFreeSlab(const LockFreeSlab&) = delete;
    LockFreeSlab& operator=(const LockFreeSlab&) = delete;

    /**
     * Allocate: O(1), lock-free, wait-free en el caso común.
     * Retorna puntero al área de usuario (después del next pointer).
     * Retorna nullptr si no hay bloques disponibles.
     */
    [[gnu::always_inline]]
    void* allocate() noexcept {
        uint64_t old_head = head_.load(std::memory_order_acquire);

        while (true) {
            auto [index, tag] = unpack(old_head);

            if (index == NULL_INDEX) [[unlikely]] {
                metrics_.failed_allocations.fetch_add(1, std::memory_order_relaxed);
                return nullptr;  // Pool agotado
            }

            // Leer el next pointer del bloque actual
            uint64_t* slot = slot_ptr(index);
            uint64_t next_index = *slot;

            // Nuevo head: next_index con tag incrementado (previene ABA)
            uint64_t new_head = pack(next_index, tag + 1);

            // CAS atómico: si el head no cambió, actualizarlo
            if (head_.compare_exchange_weak(old_head, new_head,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) [[likely]] {
                // Éxito: marcar el slot como en uso (poison el next pointer)
                *slot = POISON_INDEX;

                // Actualizar métricas
                uint64_t in_use = metrics_.current_in_use.fetch_add(1, std::memory_order_relaxed) + 1;
                metrics_.allocations.fetch_add(1, std::memory_order_relaxed);

                uint64_t peak = metrics_.peak_in_use.load(std::memory_order_relaxed);
                while (in_use > peak) {
                    if (metrics_.peak_in_use.compare_exchange_weak(peak, in_use,
                        std::memory_order_relaxed)) break;
                }

                // Retornar puntero al área de usuario (después del next pointer)
                return static_cast<char*>(slot) + sizeof(uint64_t);
            }

            // CAS falló: otro hilo ganó, reintentar con el nuevo head
            _mm_pause();  // Hint a la CPU para reducir contención
        }
    }

    /**
     * Deallocate: O(1), lock-free.
     * Libera el bloque y lo añade al inicio de la free-list.
     */
    [[gnu::always_inline]]
    void deallocate(void* user_ptr) noexcept {
        if (user_ptr == nullptr) [[unlikely]] return;

        // Recuperar el slot (el next pointer está justo antes)
        uint64_t* slot = static_cast<uint64_t*>(user_ptr) - 1;

        // Validación defensiva: el slot debe estar dentro del pool
        if (!is_valid_slot(slot)) [[unlikely]] {
            metrics_.failed_allocations.fetch_add(1, std::memory_order_relaxed);
            return;  // Puntero inválido, no hacer nada (evitar corrupción)
        }

        // Detección de double-free
        if (*slot != POISON_INDEX) [[unlikely]] {
            // Ya estaba libre = double-free detectado
            return;
        }

        uint64_t index = slot_index(slot);
        uint64_t old_head = head_.load(std::memory_order_acquire);

        while (true) {
            auto [head_index, tag] = unpack(old_head);

            // El slot ahora apunta al head actual
            *slot = head_index;

            uint64_t new_head = pack(index, tag + 1);

            if (head_.compare_exchange_weak(old_head, new_head,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) [[likely]] {
                metrics_.current_in_use.fetch_sub(1, std::memory_order_relaxed);
                metrics_.deallocations.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            _mm_pause();
        }
    }

    [[gnu::always_inline]]
    const Metrics& metrics() const noexcept { return metrics_; }

    [[gnu::always_inline]]
    size_t available() const noexcept {
        return num_blocks_ - metrics_.current_in_use.load(std::memory_order_relaxed);
    }

    [[gnu::always_inline]]
    size_t capacity() const noexcept { return num_blocks_; }

private:
    static constexpr uint64_t POISON_INDEX = 0xFFFFFFFFFFFFFFFEULL;

    char* base_;
    size_t block_size_;
    size_t num_blocks_;

    // Head de la free-list: [index (48 bits)][tag (16 bits)]
    alignas(64) std::atomic<uint64_t> head_;

    alignas(64) Metrics metrics_;

    // Empaqueta index + tag en un uint64_t
    [[gnu::const]] static constexpr uint64_t pack(uint64_t index, uint64_t tag) noexcept {
        return (index & 0x0000FFFFFFFFFFFFULL) | ((tag & 0xFFFFULL) << 48);
    }

    // Desempaqueta index + tag
    [[gnu::const]] static constexpr std::pair<uint64_t, uint64_t>
    unpack(uint64_t packed) noexcept {
        uint64_t index = packed & 0x0000FFFFFFFFFFFFULL;
        uint64_t tag = (packed >> 48) & 0xFFFFULL;
        return {index, tag};
    }

    [[gnu::always_inline]]
    uint64_t* slot_ptr(size_t index) noexcept {
        return reinterpret_cast<uint64_t*>(base_ + index * block_size_);
    }

    [[gnu::always_inline]]
    bool is_valid_slot(const uint64_t* slot) const noexcept {
        const char* slot_char = reinterpret_cast<const char*>(slot);
        return slot_char >= base_ && slot_char < base_ + num_blocks_ * block_size_;
    }

    [[gnu::always_inline]]
    size_t slot_index(const uint64_t* slot) const noexcept {
        return (reinterpret_cast<const char*>(slot) - base_) / block_size_;
    }
};

} // namespace effata::hft::memory
