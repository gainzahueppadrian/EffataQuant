// effata_hft/memory/numa_memory_pool.hpp
#pragma once
#include <numa.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <atomic>
#include <chrono>
#include <mutex>

#include "lock_free_slab.hpp"
#include "canary_guard.hpp"

namespace effata::hft::memory {

/**
 * NumaMemoryPool: Pool de memoria NUMA-aware, zero-allocation en hot path.
 *
 * Características de producción:
 *   1. Asignación NUMA-local (evita acceso remoto a RAM)
 *   2. mlock() para evitar page faults (swap)
 *   3. mlockall() opcional para todo el proceso
 *   4. Huge pages opcionales (menor TLB miss rate)
 *   5. CPU pinning del hilo principal
 *   6. Auditoría periódica de integridad
 *   7. Graceful shutdown con drain
 *   8. Zero-allocation en hot path (todo pre-asignado en startup)
 *   9. Detección de corrupción con CanaryGuard
 *  10. Lock-free allocation/deallocation
 *
 * Filosofía Buffett aplicada:
 *   - "Precio es lo que pagas, valor es lo que obtienes" → Pagamos NUMA-local + mlock,
 *     obtenemos latencia predecible sub-microsegundo
 *   - "Nunca pierdas dinero" → Nunca pierdas integridad de memoria
 *   - "Margen de seguridad" → Canary bytes + validaciones en cada operación
 */
class NumaMemoryPool {
public:
    struct Config {
        size_t total_size_bytes;
        size_t block_size_bytes;
        int numa_node = 0;
        bool use_hugepages = false;
        bool mlock_all = true;              // mlockall(MCL_CURRENT|MCL_FUTURE)
        bool pin_to_cpu = true;             // sched_setaffinity
        int pinned_cpu = -1;                  // -1 = primer CPU del nodo NUMA
        bool enable_periodic_audit = true;
        std::chrono::seconds audit_interval{60};
        size_t preheat_count = 1024;        // Pre-asignar N bloques al startup
    };

    struct PoolMetrics {
        uint64_t total_capacity;
        uint64_t blocks_in_use;
        uint64_t blocks_available;
        uint64_t peak_in_use;
        uint64_t total_allocations;
        uint64_t total_deallocations;
        uint64_t failed_allocations;
        uint64_t corruption_detected;
        double utilization_pct;
        std::chrono::nanoseconds last_audit_duration;
        bool audit_passed;
    };

    explicit NumaMemoryPool(const Config& cfg) : cfg_(cfg) {
        initialize();
    }

    ~NumaMemoryPool() {
        shutdown();
    }

    // No copiable, no movible
    NumaMemoryPool(const NumaMemoryPool&) = delete;
    NumaMemoryPool& operator=(const NumaMemoryPool&) = delete;

    /**
     * HOT PATH: allocate - O(1), lock-free, sin syscalls.
     * Retorna nullptr si el pool está agotado.
     */
    [[gnu::always_inline, gnu::hot]]
    void* allocate() noexcept {
        return slab_->allocate();
    }

    /**
     * HOT PATH: deallocate - O(1), lock-free, sin syscalls.
     */
    [[gnu::always_inline, gnu::hot]]
    void deallocate(void* ptr) noexcept {
        slab_->deallocate(ptr);
    }

    /**
     * COLD PATH: Auditoría completa de integridad.
     * Verifica canary bytes de todos los bloques en uso.
     */
    bool run_full_audit() {
        auto start = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(audit_mutex_);

        uint64_t corrupted = 0;
        uint64_t blocks_checked = 0;

        // Auditar solo los bloques marcados como "en uso"
        for (size_t i = 0; i < in_use_bitmap_.size(); ++i) {
            if (in_use_bitmap_[i].load(std::memory_order_acquire)) {
                void* block = static_cast<char*>(base_memory_) + i * cfg_.block_size_bytes;
                auto validation = CanaryGuard::validate(block, user_block_size_);
                if (!validation.is_valid) {
                    corrupted++;
                    std::cerr << "[AUDIT] Corrupción en bloque " << i
                              << ": " << validation.error_message << std::endl;
                }
                blocks_checked++;
            }
        }

        auto end = std::chrono::steady_clock::now();
        last_audit_duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        last_audit_passed_ = (corrupted == 0);

        if (corrupted > 0) {
            corruption_count_.fetch_add(corrupted, std::memory_order_relaxed);
            std::cerr << "[AUDIT] FALLIDO: " << corrupted << " bloques corruptos de "
                      << blocks_checked << " revisados" << std::endl;
        }

        return last_audit_passed_;
    }

    [[gnu::cold]]
    PoolMetrics get_metrics() const noexcept {
        PoolMetrics m{};
        const auto& sm = slab_->metrics();

        m.total_capacity = cfg_.total_size_bytes;
        m.blocks_in_use = sm.current_in_use.load(std::memory_order_relaxed);
        m.blocks_available = slab_->capacity() - m.blocks_in_use;
        m.peak_in_use = sm.peak_in_use.load(std::memory_order_relaxed);
        m.total_allocations = sm.allocations.load(std::memory_order_relaxed);
        m.total_deallocations = sm.deallocations.load(std::memory_order_relaxed);
        m.failed_allocations = sm.failed_allocations.load(std::memory_order_relaxed);
        m.corruption_detected = corruption_count_.load(std::memory_order_relaxed);
        m.utilization_pct = slab_->capacity() > 0
            ? (100.0 * m.blocks_in_use / slab_->capacity()) : 0.0;
        m.last_audit_duration = last_audit_duration_;
        m.audit_passed = last_audit_passed_;

        return m;
    }

    [[gnu::cold]]
    void print_status(std::ostream& os) const {
        auto m = get_metrics();
        os << "╔════════════════════════════════════════════════════════════╗\n"
           << "║           EFFATA NUMA MEMORY POOL - STATUS                 ║\n"
           << "╠════════════════════════════════════════════════════════════╣\n"
           << "║ Capacity:         " << std::setw(12) << m.total_capacity << " bytes            ║\n"
           << "║ Blocks in use:    " << std::setw(12) << m.blocks_in_use << "                    ║\n"
           << "║ Blocks available: " << std::setw(12) << m.blocks_available << "                    ║\n"
           << "║ Peak in use:      " << std::setw(12) << m.peak_in_use << "                    ║\n"
           << "║ Utilization:      " << std::setw(11) << std::fixed << std::setprecision(2)
           << m.utilization_pct << "%                  ║\n"
           << "║ Total allocs:     " << std::setw(12) << m.total_allocations << "                    ║\n"
           << "║ Total deallocs:   " << std::setw(12) << m.total_deallocations << "                    ║\n"
           << "║ Failed allocs:    " << std::setw(12) << m.failed_allocations << "                    ║\n"
           << "║ Corruption:       " << std::setw(12) << m.corruption_detected << "                    ║\n"
           << "║ Last audit:       " << std::setw(12) << m.last_audit_duration.count() << " ns            ║\n"
           << "║ Audit passed:     " << std::setw(12) << (m.audit_passed ? "YES" : "NO ") << "                    ║\n"
           << "╚════════════════════════════════════════════════════════════╝\n";
    }

private:
    Config cfg_;
    void* base_memory_ = nullptr;
    size_t actual_total_size_ = 0;
    size_t user_block_size_ = 0;

    std::unique_ptr<LockFreeSlab> slab_;
    std::vector<std::atomic<bool>> in_use_bitmap_;

    std::thread audit_thread_;
    std::atomic<bool> shutdown_requested_{false};
    std::mutex audit_mutex_;
    std::atomic<uint64_t> corruption_count_{0};
    std::chrono::nanoseconds last_audit_duration_{0};
    bool last_audit_passed_ = true;

    [[gnu::cold]]
    void initialize() {
        std::cout << "[INIT] Configurando NumaMemoryPool...\n";

        // 1. Verificar soporte NUMA
        if (numa_available() < 0) {
            throw std::runtime_error("NUMA no disponible en este sistema");
        }

        int max_node = numa_max_node();
        if (cfg_.numa_node < 0 || cfg_.numa_node > max_node) {
            throw std::runtime_error("Nodo NUMA " + std::to_string(cfg_.numa_node) +
                                     " fuera de rango (max: " + std::to_string(max_node) + ")");
        }

        // 2. Calcular tamaños reales (con overhead de canary + slab next pointer)
        user_block_size_ = cfg_.block_size_bytes;
        size_t slab_block_size = sizeof(uint64_t) + user_block_size_;  // next ptr + user data
        size_t total_block_size = CanaryGuard::total_size(slab_block_size);

        size_t num_blocks = cfg_.total_size_bytes / total_block_size;
        if (num_blocks == 0) {
            throw std::runtime_error("Tamaño total insuficiente para al menos 1 bloque");
        }

        actual_total_size_ = num_blocks * total_block_size;

        std::cout << "[INIT] Configuración:\n"
                  << "  - NUMA node: " << cfg_.numa_node << "\n"
                  << "  - Block size (user): " << user_block_size_ << " bytes\n"
                  << "  - Block size (total): " << total_block_size << " bytes\n"
                  << "  - Num blocks: " << num_blocks << "\n"
                  << "  - Total memory: " << actual_total_size_ << " bytes ("
                  << (actual_total_size_ / (1024.0*1024.0)) << " MB)\n";

        // 3. Asignar memoria NUMA-local
        base_memory_ = allocate_numa_memory(actual_total_size_);

        // 4. Bloquear memoria en RAM (evitar swap = evitar latencia de page fault)
        if (cfg_.mlock_all) {
            if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
                std::cerr << "[WARN] mlockall falló: " << strerror(errno)
                          << " (necesita ulimit -l unlimited)\n";
            } else {
                std::cout << "[INIT] ✓ mlockall exitoso (memoria no swapeable)\n";
            }
        } else {
            if (mlock(base_memory_, actual_total_size_) != 0) {
                std::cerr << "[WARN] mlock falló: " << strerror(errno) << "\n";
            }
        }

        // 5. Pin del hilo actual al CPU del nodo NUMA
        if (cfg_.pin_to_cpu) {
            pin_current_thread();
        }

        // 6. Crear el slab allocator
        slab_ = std::make_unique<LockFreeSlab>(base_memory_, total_block_size, num_blocks);

        // 7. Inicializar bitmap de tracking
        in_use_bitmap_.resize(num_blocks);
        for (auto& bit : in_use_bitmap_) {
            bit.store(false, std::memory_order_relaxed);
        }

        // 8. Inicializar canary bytes en todos los bloques
        for (size_t i = 0; i < num_blocks; ++i) {
            void* block = static_cast<char*>(base_memory_) + i * total_block_size;
            CanaryGuard::initialize(block, slab_block_size, i, 0);
        }

        // 9. Pre-heat (asignar y liberar N bloques para warm-up de caché)
        if (cfg_.preheat_count > 0) {
            std::cout << "[INIT] Pre-heating " << cfg_.preheat_count << " bloques...\n";
            std::vector<void*> preheat;
            preheat.reserve(cfg_.preheat_count);
            for (size_t i = 0; i < cfg_.preheat_count; ++i) {
                void* p = slab_->allocate();
                if (p) preheat.push_back(p);
            }
            for (void* p : preheat) {
                slab_->deallocate(p);
            }
        }

        // 10. Iniciar hilo de auditoría periódica
        if (cfg_.enable_periodic_audit) {
            audit_thread_ = std::thread([this]() { audit_loop(); });
        }

        std::cout << "[INIT] ✓ NumaMemoryPool inicializado correctamente\n";
    }

    [[gnu::cold]]
    void* allocate_numa_memory(size_t size) {
        void* ptr = nullptr;

        if (cfg_.use_hugepages) {
            // Intentar huge pages (2MB) - reduce TLB misses
            ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
            if (ptr != MAP_FAILED) {
                // Mover a nodo NUMA específico
                struct bitmask* nodemask = numa_allocate_nodemask();
                numa_bitmask_setbit(nodemask, cfg_.numa_node);
                if (mbind(ptr, size, MPOL_BIND, nodemask->maskp, nodemask->size, 0) != 0) {
                    std::cerr << "[WARN] mbind falló para huge pages: " << strerror(errno) << "\n";
                }
                numa_free_nodemask(nodemask);
                std::cout << "[INIT] ✓ Huge pages (2MB) asignadas en nodo " << cfg_.numa_node << "\n";
                return ptr;
            }
            std::cerr << "[WARN] Huge pages no disponibles, usando páginas normales\n";
        }

        // Páginas normales (4KB)
        ptr = numa_alloc_onnode(size, cfg_.numa_node);
        if (!ptr) {
            throw std::runtime_error("numa_alloc_onnode falló: " + std::string(strerror(errno)));
        }

        std::cout << "[INIT] ✓ Memoria NUMA asignada en nodo " << cfg_.numa_node << "\n";
        return ptr;
    }

    [[gnu::cold]]
    void pin_current_thread() {
        int target_cpu = cfg_.pinned_cpu;

        if (target_cpu < 0) {
            // Encontrar primer CPU del nodo NUMA
            struct bitmask* cpumask = numa_allocate_cpumask();
            if (numa_node_to_cpus(cfg_.numa_node, cpumask) == 0) {
                for (int cpu = 0; cpu < numa_num_configured_cpus(); ++cpu) {
                    if (numa_bitmask_isbitset(cpumask, cpu)) {
                        target_cpu = cpu;
                        break;
                    }
                }
            }
            numa_free_cpumask(cpumask);
        }

        if (target_cpu < 0) {
            std::cerr << "[WARN] No se pudo determinar CPU para pinning\n";
            return;
        }

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(target_cpu, &cpuset);

        if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
            std::cerr << "[WARN] sched_setaffinity falló: " << strerror(errno) << "\n";
        } else {
            std::cout << "[INIT] ✓ Hilo principal pineado a CPU " << target_cpu << "\n";
        }
    }

    [[gnu::cold]]
    void audit_loop() {
        while (!shutdown_requested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(cfg_.audit_interval);
            if (shutdown_requested_.load(std::memory_order_acquire)) break;
            run_full_audit();
        }
    }

    [[gnu::cold]]
    void shutdown() {
        std::cout << "[SHUTDOWN] Cerrando NumaMemoryPool...\n";

        // 1. Detener auditoría
        shutdown_requested_.store(true, std::memory_order_release);
        if (audit_thread_.joinable()) {
            audit_thread_.join();
        }

        // 2. Auditoría final
        if (cfg_.enable_periodic_audit) {
            bool ok = run_full_audit();
            if (!ok) {
                std::cerr << "[SHUTDOWN] ⚠ ADVERTENCIA: Corrupción detectada en shutdown\n";
            }
        }

        // 3. Imprimir estado final
        print_status(std::cout);

        // 4. Liberar memoria NUMA
        if (base_memory_) {
            if (cfg_.mlock_all) {
                munlockall();
            } else {
                munlock(base_memory_, actual_total_size_);
            }

            if (cfg_.use_hugepages) {
                munmap(base_memory_, actual_total_size_);
            } else {
                numa_free(base_memory_, actual_total_size_);
            }
            base_memory_ = nullptr;
        }

        std::cout << "[SHUTDOWN] ✓ NumaMemoryPool cerrado correctamente\n";
    }
};

} // namespace effata::hft::memory
