#pragma once
#include <iostream>
#include <cstdlib>
#include <chrono>

namespace effata::hft::memory {

class NumaMemoryPool {
public:
    struct Config {
        size_t total_size_bytes = 0;
        size_t block_size_bytes = 0;
        int numa_node = 0;
        bool use_hugepages = false;
        bool mlock_all = false;
        bool pin_to_cpu = false;
        int pinned_cpu = -1;
        bool enable_periodic_audit = false;
        std::chrono::seconds audit_interval{60};
        size_t preheat_count = 0;
    };

    explicit NumaMemoryPool(const Config& cfg) : cfg_(cfg) {
        std::cout << "[INIT] NumaMemoryPool inicializado (Fallback a malloc)\n";
    }

    ~NumaMemoryPool() {
        std::cout << "[SHUTDOWN] NumaMemoryPool cerrado.\n";
    }

    NumaMemoryPool(const NumaMemoryPool&) = delete;
    NumaMemoryPool& operator=(const NumaMemoryPool&) = delete;

    [[gnu::always_inline, gnu::hot]]
    void* allocate() noexcept {
        return std::malloc(cfg_.block_size_bytes);
    }

    [[gnu::always_inline, gnu::hot]]
    void deallocate(void* ptr) noexcept {
        if (ptr) {
            std::free(ptr);
        }
    }

    void print_status(std::ostream& os) const {
        os << "[MEMORY] Status: Malloc Fallback Active.\n";
    }

private:
    Config cfg_;
};

} // namespace effata::hft::memory
