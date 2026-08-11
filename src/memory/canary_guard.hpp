// effata_hft/memory/canary_guard.hpp
#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#include <bit>
#include <stdexcept>

namespace effata::hft::memory {

/**
 * CanaryGuard: Protección contra buffer overflow/underflow y use-after-free.
 *
 * Filosofía Buffett: "Regla #1: No pierdas dinero. Regla #2: No olvides la Regla #1"
 * Traducción a memoria: "Regla #1: No corrompas memoria. Regla #2: Detecta si lo haces"
 *
 * Estructura de cada bloque:
 * [CANARY_PRE (8B)][USER DATA (N bytes)][CANARY_POST (8B)][METADATA (8B)]
 */
class CanaryGuard {
public:
    static constexpr uint64_t LIVE_CANARY  = 0xDEADBEEFCAFEBABEULL;
    static constexpr uint64_t FREE_CANARY  = 0xFEEDFACEBAADF00DULL;
    static constexpr uint64_t POISON_BYTE  = 0xCDCDCDCDCDCDCDCDULL;

    struct BlockHeader {
        uint64_t pre_canary;
        uint64_t block_id;
        uint64_t allocation_generation;  // Para detectar ABA problem
    };

    struct BlockFooter {
        uint64_t post_canary;
        uint64_t checksum;  // CRC de los datos para integridad
    };

    static constexpr size_t HEADER_SIZE = sizeof(BlockHeader);
    static constexpr size_t FOOTER_SIZE = sizeof(BlockFooter);
    static constexpr size_t OVERHEAD    = HEADER_SIZE + FOOTER_SIZE;

    // Calcula el tamaño total necesario para un bloque de usuario
    [[gnu::const]] static constexpr size_t total_size(size_t user_size) noexcept {
        // Alinear a 64 bytes (cacheline) para evitar false sharing
        size_t raw = user_size + OVERHEAD;
        return (raw + 63ULL) & ~63ULL;
    }

    // Inicializa un bloque recién asignado
    [[gnu::always_inline]]
    static void initialize(void* raw_ptr, size_t user_size,
                          uint64_t block_id, uint64_t generation) noexcept {
        auto* header = static_cast<BlockHeader*>(raw_ptr);
        header->pre_canary = LIVE_CANARY;
        header->block_id = block_id;
        header->allocation_generation = generation;

        // Datos de usuario: inicializar a 0 (seguridad defensiva)
        void* user_data = user_ptr(raw_ptr);
        std::memset(user_data, 0, user_size);

        // Footer
        auto* footer = footer_ptr(raw_ptr, user_size);
        footer->post_canary = LIVE_CANARY;
        footer->checksum = compute_checksum(user_data, user_size);
    }

    // Marca un bloque como liberado (poisoning)
    [[gnu::always_inline]]
    static void mark_freed(void* raw_ptr, size_t user_size) noexcept {
        auto* header = static_cast<BlockHeader*>(raw_ptr);
        header->pre_canary = FREE_CANARY;

        // Poison los datos del usuario para detectar use-after-free
        void* user_data = user_ptr(raw_ptr);
        std::memset(user_data, 0xCD, user_size);  // 0xCD = freed memory

        auto* footer = footer_ptr(raw_ptr, user_size);
        footer->post_canary = FREE_CANARY;
    }

    // Verifica integridad del bloque (llamado en auditoría)
    struct ValidationResult {
        bool is_valid;
        bool is_live;
        bool pre_canary_ok;
        bool post_canary_ok;
        bool checksum_ok;
        const char* error_message;
    };

    [[gnu::always_inline]]
    static ValidationResult validate(const void* raw_ptr, size_t user_size) noexcept {
        ValidationResult result{true, false, false, false, false, "OK"};

        const auto* header = static_cast<const BlockHeader*>(raw_ptr);
        const auto* footer = footer_ptr(raw_ptr, user_size);

        result.pre_canary_ok = (header->pre_canary == LIVE_CANARY);
        result.post_canary_ok = (footer->post_canary == LIVE_CANARY);
        result.is_live = result.pre_canary_ok && result.post_canary_ok;

        if (!result.is_live) {
            if (header->pre_canary == FREE_CANARY) {
                result.error_message = "USE-AFTER-FREE detected";
            } else {
                result.error_message = "CORRUPTED CANARY (overflow/underflow)";
            }
            result.is_valid = false;
            return result;
        }

        // Verificar checksum
        const void* user_data = user_ptr(raw_ptr);
        uint64_t expected = compute_checksum(user_data, user_size);
        result.checksum_ok = (footer->checksum == expected);

        if (!result.checksum_ok) {
            result.error_message = "DATA CORRUPTION (silent bit flip or bug)";
            result.is_valid = false;
        }

        return result;
    }

    // Puntero al área de usuario
    [[gnu::always_inline]]
    static void* user_ptr(void* raw_ptr) noexcept {
        return static_cast<char*>(raw_ptr) + HEADER_SIZE;
    }

    [[gnu::always_inline]]
    static const void* user_ptr(const void* raw_ptr) noexcept {
        return static_cast<const char*>(raw_ptr) + HEADER_SIZE;
    }

    // Puntero al footer
    [[gnu::always_inline]]
    static BlockFooter* footer_ptr(void* raw_ptr, size_t user_size) noexcept {
        return reinterpret_cast<BlockFooter*>(
            static_cast<char*>(raw_ptr) + HEADER_SIZE + user_size);
    }

    [[gnu::always_inline]]
    static const BlockFooter* footer_ptr(const void* raw_ptr, size_t user_size) noexcept {
        return reinterpret_cast<const BlockFooter*>(
            static_cast<const char*>(raw_ptr) + HEADER_SIZE + user_size);
    }

private:
    // FNV-1a checksum rápido (sin allocations, sin syscalls)
    [[gnu::const]]
    static uint64_t compute_checksum(const void* data, size_t size) noexcept {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = 0xcbf29ce484222325ULL;

        // Procesar 8 bytes a la vez cuando sea posible
        size_t full_words = size / 8;
        const uint64_t* words = reinterpret_cast<const uint64_t*>(bytes);

        for (size_t i = 0; i < full_words; ++i) {
            hash ^= words[i];
            hash *= 0x100000001b3ULL;
        }

        // Bytes restantes
        for (size_t i = full_words * 8; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 0x100000001b3ULL;
        }

        return hash;
    }
};

} // namespace effata::hft::memory
