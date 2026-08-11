// effata_hft/network/hardware_timestamped_packet.hpp
#pragma once
#include <chrono>
#include <cstdint>

namespace effata::hft {

// Estructura optimizada para caber en una sola línea de caché (64 bytes)
struct alignas(64) MarketTick {
    uint64_t symbol_id;          // 8 bytes (ID numérico, no string)
    uint64_t exchange_timestamp; // 8 bytes (Nanosegundos desde epoch, hardware timestamp)
    uint64_t local_recv_timestamp; // 8 bytes (Nanosegundos de llegada a la NIC)
    double price;                // 8 bytes
    uint32_t volume;             // 4 bytes
    uint8_t  side;               // 1 byte (0=Buy, 1=Sell)
    uint8_t  flags;              // 1 byte (Para flags de intercambio)
    // 34 bytes de relleno para alinear a 64 bytes y evitar false sharing
    uint8_t  padding[34];
};

// Función para calcular la latencia de red (Wire-to-Logic) en nanosegundos
[[gnu::always_inline]] inline uint64_t calculate_wire_to_logic_latency(const MarketTick& tick) noexcept {
    if (tick.local_recv_timestamp < tick.exchange_timestamp) {
        return 0; // Clock skew o error de hardware
    }
    return tick.local_recv_timestamp - tick.exchange_timestamp;
}

} // namespace effata::hft
