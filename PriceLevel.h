#pragma once

#include <cstddef>
#include <cstdint>

#include "Order.h"

struct PriceLevel
{
    Order* head = nullptr;
    Order* tail = nullptr;
    uint64_t total_quantity = 0;
    size_t order_count = 0;
};

inline void appendOrder(PriceLevel& level, Order* order)
{
    order->prev = level.tail;
    order->next = nullptr;

    if (level.tail)
        level.tail->next = order;
    else
        level.head = order;

    level.tail = order;
    level.total_quantity += order->quantity;
    ++level.order_count;
}

inline void unlinkOrder(PriceLevel& level, Order* order)
{
    if (order->prev)
        order->prev->next = order->next;
    else
        level.head = order->next;

    if (order->next)
        order->next->prev = order->prev;
    else
        level.tail = order->prev;

    order->prev = nullptr;
    order->next = nullptr;
    --level.order_count;
}
