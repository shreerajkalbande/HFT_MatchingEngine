#pragma once
#include <cstdint>

enum class Side : uint8_t
{
    BID,
    ASK
};

// Orders are owned by one matching thread. prev/next form an intrusive FIFO
// inside a price level, avoiding one std::list allocation per resting order.
struct Order
{
    uint64_t id;
    uint64_t timestamp;
    Order* prev;
    Order* next;
    uint32_t price;
    uint32_t quantity;
    uint32_t arena_index;
    Side side;
    bool is_active;
};

static_assert(sizeof(Order) <= 64, "Order should fit in one cache line");
