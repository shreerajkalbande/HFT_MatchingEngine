#include "OrderArena.h"
#include <iostream>

OrderArena::OrderArena(size_t max_orders)
    : arena(max_orders), next_free_index(0)
{
}

Order* OrderArena::allocateOrder(uint64_t id, uint32_t price, uint32_t qty, Side side, uint64_t ts)
{
    if (next_free_index >= arena.size())
    {
        std::cerr << "CRITICAL: Arena Out of Memory!\n";
        return nullptr;
    }

    Order* order = &arena[next_free_index++];
    order->id = id;
    order->price = price;
    order->quantity = qty;
    order->side = side;
    order->timestamp = ts;
    order->is_active = true;
    return order;
}

void OrderArena::reset()
{
    next_free_index = 0;
}
