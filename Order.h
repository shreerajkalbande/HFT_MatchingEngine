#pragma once
#include <cstdint>

enum class Side : uint8_t
{
    BID,
    ASK
};

enum class OrderType : uint8_t
{
    NEW,
    CANCEL
};

// Each Order occupies exactly one cache line (64 bytes) to prevent
// false sharing and ensure a single fetch loads all fields.
struct alignas(64) Order
{
    uint64_t id;
    uint64_t timestamp;
    uint32_t price;
    uint32_t quantity;
    Side side;
    bool is_active;
    // 38 bytes of padding to fill cache line (implicit from alignas)
};

// Represents the raw data coming off the wire
struct UDPPacket
{
    uint64_t id;
    uint32_t price;
    uint32_t quantity;
    Side side;
    OrderType type;
    uint64_t timestamp;
};
