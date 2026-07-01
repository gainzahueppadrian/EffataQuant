#pragma once

#include <cstdint>
#include "../security/enclave_signer.hpp"

namespace berkshire::core {

struct alignas(64) OrderMessage {
    uint64_t id;
    uint32_t ticker_hash;
    uint32_t quantity;
    double limit_price;
    uint8_t  broker_id;
    uint8_t  strategy_type;
    uint8_t  action;

    security::Signature signature;

    char     padding[5];

    OrderMessage() = default;
    OrderMessage(uint64_t _id, uint32_t _hash, uint32_t _qty, double _price, uint8_t _broker, uint8_t _strategy, uint8_t _action)
        : id(_id), ticker_hash(_hash), quantity(_qty), limit_price(_price), broker_id(_broker), strategy_type(_strategy), action(_action) {}
};

static_assert(sizeof(OrderMessage) == 64, "OrderMessage must be exactly 64 bytes for cache line alignment");
}