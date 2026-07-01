#pragma once

#include <cstdint>
#include <array>
#include <iostream>
#include <string>

#define HOT [[gnu::hot]]
#define ALWAYS_INLINE [[gnu::always_inline]] inline

namespace berkshire::security {

/**
 * @brief Represents a cryptographic signature appended to orders.
 */
using Signature = std::array<uint8_t, 32>;

/**
 * @brief Trusted Execution Environment (TEE) Order Signer
 * Simulates Intel SGX / AMD SEV enclave signing to prevent MITM and Order Mutilation.
 * In a real hardware environment, this runs inside a secure memory enclave.
 */
class EnclaveSigner {
public:
    EnclaveSigner() {
        std::cout << "[SECURITY] TEE Enclave initialized. Cryptographic bounds active." << std::endl;
    }

    /**
     * @brief Signs the binary payload of an order in sub-microseconds.
     * Uses a fast hardware-based HMAC or simulated SHA-256 equivalent.
     */
    HOT ALWAYS_INLINE Signature sign_payload(const void* data, size_t length) const {
        Signature sig;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);

        // Simulated fast hash / HMAC (FNV-1a variant for speed in this mock)
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < length; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }

        // Fill signature buffer
        for(int i = 0; i < 8; ++i) {
            sig[i] = (hash >> (i * 8)) & 0xFF;
            sig[i+8] = ((hash ^ 0xDEADBEEF) >> (i * 8)) & 0xFF;
            sig[i+16] = ((hash ^ 0xCAFEBABE) >> (i * 8)) & 0xFF;
            sig[i+24] = ((hash ^ 0x8BADF00D) >> (i * 8)) & 0xFF;
        }

        return sig;
    }

    /**
     * @brief Validates if a signature matches the payload.
     */
    HOT ALWAYS_INLINE bool verify_signature(const void* data, size_t length, const Signature& sig) const {
        Signature expected = sign_payload(data, length);
        for(size_t i = 0; i < 32; ++i) {
            if(expected[i] != sig[i]) return false;
        }
        return true;
    }
};

} // namespace berkshire::security