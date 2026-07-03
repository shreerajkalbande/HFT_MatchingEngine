#include "OrderBook.h"

#include <algorithm>
#include <cassert>

MapOrderBook::MapOrderBook(size_t expected_resting_orders)
{
    if (expected_resting_orders > 0)
        order_lookup.reserve(expected_resting_orders);
}

void MapOrderBook::releaseOrder(Order* order)
{
    if (arena)
        arena->deallocateOrder(order);
    else
    {
        order->is_active = false;
        order->prev = nullptr;
        order->next = nullptr;
    }
}

bool MapOrderBook::insertBid(Order* order)
{
    if (!order || order->side != Side::BID || !order->is_active ||
        order->price == 0 || order->quantity == 0 || contains(order->id))
        return false;

    auto [lookup_it, inserted] = order_lookup.emplace(order->id, order);
    if (!inserted)
        return false;
    (void)lookup_it;

    auto [level_it, new_level] = bids.try_emplace(order->price);
    (void)new_level;
    appendOrder(level_it->second, order);
    ++bid_order_count;
    return true;
}

bool MapOrderBook::insertAsk(Order* order)
{
    if (!order || order->side != Side::ASK || !order->is_active ||
        order->price == 0 || order->quantity == 0 || contains(order->id))
        return false;

    auto [lookup_it, inserted] = order_lookup.emplace(order->id, order);
    if (!inserted)
        return false;
    (void)lookup_it;

    auto [level_it, new_level] = asks.try_emplace(order->price);
    (void)new_level;
    appendOrder(level_it->second, order);
    ++ask_order_count;
    return true;
}

Order* MapOrderBook::bestBid() const
{
    return bids.empty() ? nullptr : bids.begin()->second.head;
}

Order* MapOrderBook::bestAsk() const
{
    return asks.empty() ? nullptr : asks.begin()->second.head;
}

void MapOrderBook::fillBestBid(uint32_t quantity)
{
    assert(!bids.empty());
    auto level_it = bids.begin();
    PriceLevel& level = level_it->second;
    Order* order = level.head;
    assert(order && quantity > 0 && quantity <= order->quantity);

    order->quantity -= quantity;
    level.total_quantity -= quantity;

    if (order->quantity == 0)
    {
        unlinkOrder(level, order);
        order_lookup.erase(order->id);
        --bid_order_count;
        if (level.order_count == 0)
            bids.erase(level_it);
        releaseOrder(order);
    }
}

void MapOrderBook::fillBestAsk(uint32_t quantity)
{
    assert(!asks.empty());
    auto level_it = asks.begin();
    PriceLevel& level = level_it->second;
    Order* order = level.head;
    assert(order && quantity > 0 && quantity <= order->quantity);

    order->quantity -= quantity;
    level.total_quantity -= quantity;

    if (order->quantity == 0)
    {
        unlinkOrder(level, order);
        order_lookup.erase(order->id);
        --ask_order_count;
        if (level.order_count == 0)
            asks.erase(level_it);
        releaseOrder(order);
    }
}

bool MapOrderBook::cancelOrder(uint64_t order_id)
{
    auto lookup_it = order_lookup.find(order_id);
    if (lookup_it == order_lookup.end())
        return false;

    Order* order = lookup_it->second;
    if (order->side == Side::BID)
    {
        auto level_it = bids.find(order->price);
        assert(level_it != bids.end());
        PriceLevel& level = level_it->second;
        level.total_quantity -= order->quantity;
        unlinkOrder(level, order);
        --bid_order_count;
        if (level.order_count == 0)
            bids.erase(level_it);
    }
    else
    {
        auto level_it = asks.find(order->price);
        assert(level_it != asks.end());
        PriceLevel& level = level_it->second;
        level.total_quantity -= order->quantity;
        unlinkOrder(level, order);
        --ask_order_count;
        if (level.order_count == 0)
            asks.erase(level_it);
    }

    order_lookup.erase(lookup_it);
    releaseOrder(order);
    return true;
}

bool MapOrderBook::contains(uint64_t order_id) const
{
    return order_lookup.find(order_id) != order_lookup.end();
}

BBO MapOrderBook::getBBO() const
{
    BBO bbo{};
    if (!bids.empty())
    {
        bbo.best_bid_price = bids.begin()->first;
        bbo.best_bid_qty = bids.begin()->second.total_quantity;
    }
    if (!asks.empty())
    {
        bbo.best_ask_price = asks.begin()->first;
        bbo.best_ask_qty = asks.begin()->second.total_quantity;
    }
    return bbo;
}

void MapOrderBook::reset()
{
    for (auto& [id, order] : order_lookup)
    {
        (void)id;
        releaseOrder(order);
    }

    bids.clear();
    asks.clear();
    order_lookup.clear();
    bid_order_count = 0;
    ask_order_count = 0;
}
