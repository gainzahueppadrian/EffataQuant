#pragma once
#include "numa_memory_pool.hpp"

namespace effata::hft::memory {

template <typename T>
class HftAllocator {
public:
    explicit HftAllocator(NumaMemoryPool& pool) : pool_(pool) {}

    T* allocate() {
        void* ptr = pool_.allocate();
        if (ptr) {
            return new (ptr) T();
        }
        return nullptr;
    }

    void deallocate(T* ptr) {
        if (ptr) {
            ptr->~T();
            pool_.deallocate(ptr);
        }
    }

private:
    NumaMemoryPool& pool_;
};

} // namespace effata::hft::memory
