#pragma once
#include <vector>
#include <unordered_map>
#include "Order.h"

struct BBO
{
    uint32_t best_bid_price = 0;
    uint32_t best_bid_qty = 0;
    uint32_t best_ask_price = 0;
    uint32_t best_ask_qty = 0;
};

class OrderBook
{
private:
    std::vector<Order*> bids;  // sorted ascending by price; best bid at back()
    std::vector<Order*> asks;  // sorted descending by price; best ask at back()
    std::unordered_map<uint64_t, Order*> order_lookup;

public:
    void insertBid(Order* order);
    void insertAsk(Order* order);

    Order* bestBid();
    Order* bestAsk();
    void removeBestBid();
    void removeBestAsk();

    bool cancelOrder(uint64_t order_id);

    BBO getBBO() const;
    size_t bidDepth() const { return bids.size(); }
    size_t askDepth() const { return asks.size(); }
    size_t totalOrders() const { return order_lookup.size(); }

    bool bidsEmpty() const { return bids.empty(); }
    bool asksEmpty() const { return asks.empty(); }

    void reset();
};
