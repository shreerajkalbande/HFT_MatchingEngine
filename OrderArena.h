#pragma once
#include <vector>
#include "Order.h"

class OrderArena
{
private:
    std::vector<Order> arena;
    size_t next_free_index;

public:
    explicit OrderArena(size_t max_orders);
    Order* allocateOrder(uint64_t id, uint32_t price, uint32_t qty, Side side, uint64_t ts);
    void reset();
    size_t size() const { return next_free_index; }
    size_t capacity() const { return arena.size(); }
};
