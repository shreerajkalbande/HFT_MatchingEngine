#include "OrderArena.h"
#include <iostream>

OrderArena::OrderArena(size_t max_orders)
    : arena(max_orders), next_free_index(0)
{
    free_list.reserve(max_orders);
}

Order* OrderArena::allocateOrder(uint64_t id, uint32_t price, uint32_t qty, Side side, uint64_t ts)
{
    size_t index;
    if (!free_list.empty())
    {
        index = free_list.back();
        free_list.pop_back();
    }
    else if (next_free_index < arena.size())
    {
        index = next_free_index++;
    }
    else
    {
        std::cerr << "CRITICAL: Arena Out of Memory!\n";
        return nullptr;
    }

    Order* order = &arena[index];
    order->id = id;
    order->price = price;
    order->quantity = qty;
    order->side = side;
    order->timestamp = ts;
    order->prev = nullptr;
    order->next = nullptr;
    order->arena_index = static_cast<uint32_t>(index);
    order->is_active = true;
    return order;
}

bool OrderArena::deallocateOrder(Order* order)
{
    if (!order || !order->is_active)
        return false;

    order->is_active = false;
    order->prev = nullptr;
    order->next = nullptr;
    free_list.push_back(order->arena_index);
    return true;
}

void OrderArena::reset()
{
    for (size_t i = 0; i < next_free_index; ++i)
    {
        arena[i].is_active = false;
        arena[i].prev = nullptr;
        arena[i].next = nullptr;
    }
    next_free_index = 0;
    free_list.clear();
}
