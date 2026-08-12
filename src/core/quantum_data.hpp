#pragma once
#include <cstdint>

namespace effata::core {

#define ALIGN_64 alignas(64)

// ============================================================================
// ESTRUCTURAS BIT-PACKED + ALINEADAS
// ============================================================================
struct alignas(64) MarketData {
    uint64_t ts_ns;
    double   spot;
    double   strike;
    double   iv;
    uint32_t oi;
    uint32_t volume;
    uint16_t expiry_days;           // Hasta 65535 días
    uint8_t  flags;                 // bit0: is_call, bit1: is_earnings
    uint8_t  _pad[21];               // padding explícito a 64B exactos

    [[nodiscard]] bool is_call()     const noexcept { return flags & 0x01; }
    [[nodiscard]] bool is_earnings() const noexcept { return flags & 0x02; }
};
static_assert(sizeof(MarketData) == 64, "MarketData debe ser exactamente 64B (1 cache line)");

struct alignas(64) ModelParams {
    double kappa, theta, xi, rho;
    double lambda_j, mu_j, sigma_j;
    double r, q;
    double max_loss_pct   = 0.001;
    double es_confidence  = 0.99;

    // Precomputados
    double kappa_j_comp;            // E[J]-1 = exp(μ+σ²/2)-1
    double sigma_j_sq;              // σ_j² cacheada
};

} // namespace effata::core
