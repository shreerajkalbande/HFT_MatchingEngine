#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>

#include "BookTypes.h"
#include "Order.h"
#include "OrderArena.h"
#include "PriceLevel.h"

// Reference price-time-priority book.
//
// Prices are indexed by a tree (L = active price levels). Each price level is
// an intrusive FIFO of Orders, so matching and unlinking an order do not
// allocate and do not shift unrelated orders.
class MapOrderBook
{
private:
    using BidLevels = std::map<uint32_t, PriceLevel, std::greater<uint32_t>>;
    using AskLevels = std::map<uint32_t, PriceLevel>;

    BidLevels bids; // begin() is the highest bid
    AskLevels asks; // begin() is the lowest ask
    std::unordered_map<uint64_t, Order*> order_lookup;
    size_t bid_order_count = 0;
    size_t ask_order_count = 0;
    OrderArena* arena = nullptr;
    void releaseOrder(Order* order);

public:
    explicit MapOrderBook(size_t expected_resting_orders = 0);
    void setArena(OrderArena* order_arena) { arena = order_arena; }

    bool insertBid(Order* order);
    bool insertAsk(Order* order);

    Order* bestBid() const;
    Order* bestAsk() const;
    void fillBestBid(uint32_t quantity);
    void fillBestAsk(uint32_t quantity);

    bool cancelOrder(uint64_t order_id);
    bool contains(uint64_t order_id) const;
    bool acceptsPrice(uint32_t price) const { return price > 0; }

    BBO getBBO() const;
    size_t bidDepth() const { return bid_order_count; }
    size_t askDepth() const { return ask_order_count; }
    size_t bidLevels() const { return bids.size(); }
    size_t askLevels() const { return asks.size(); }
    size_t totalOrders() const { return order_lookup.size(); }

    bool bidsEmpty() const { return bids.empty(); }
    bool asksEmpty() const { return asks.empty(); }

    void reset();
};

// Backward-compatible name for the reference backend.
using OrderBook = MapOrderBook;
