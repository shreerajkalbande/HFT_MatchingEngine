#pragma once
#include <vector>
#include "Order.h"

class OrderArena
{
private:
    std::vector<Order> arena;
    size_t next_free_index;
    std::vector<uint32_t> free_list;

public:
    explicit OrderArena(size_t max_orders);
    Order* allocateOrder(uint64_t id, uint32_t price, uint32_t qty, Side side, uint64_t ts);
    bool deallocateOrder(Order* order);
    void reset();
    size_t size() const { return next_free_index - free_list.size(); }
    size_t highWaterMark() const { return next_free_index; }
    size_t recycledSlots() const { return free_list.size(); }
    size_t capacity() const { return arena.size(); }
};
