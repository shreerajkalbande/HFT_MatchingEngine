#include "OrderBook.h"
#include <algorithm>

// Bids: sorted ascending by price. lower_bound inserts new orders at the
// beginning of their price group → older orders stay closer to back() → FIFO.
// Matching takes from back() → highest price, oldest first = price-time priority.
void OrderBook::insertBid(Order* order)
{
    auto it = std::lower_bound(bids.begin(), bids.end(), order,
        [](const Order* resting, const Order* incoming)
        {
            return resting->price < incoming->price;
        });
    bids.insert(it, order);
    order_lookup[order->id] = order;
}

// Asks: sorted descending by price. lower_bound inserts new orders at the
// beginning of their price group → older orders stay closer to back() → FIFO.
// Matching takes from back() → lowest price, oldest first = price-time priority.
void OrderBook::insertAsk(Order* order)
{
    auto it = std::lower_bound(asks.begin(), asks.end(), order,
        [](const Order* resting, const Order* incoming)
        {
            return resting->price > incoming->price;
        });
    asks.insert(it, order);
    order_lookup[order->id] = order;
}

Order* OrderBook::bestBid()
{
    return bids.empty() ? nullptr : bids.back();
}

Order* OrderBook::bestAsk()
{
    return asks.empty() ? nullptr : asks.back();
}

void OrderBook::removeBestBid()
{
    if (!bids.empty())
    {
        order_lookup.erase(bids.back()->id);
        bids.pop_back();
    }
}

void OrderBook::removeBestAsk()
{
    if (!asks.empty())
    {
        order_lookup.erase(asks.back()->id);
        asks.pop_back();
    }
}

bool OrderBook::cancelOrder(uint64_t order_id)
{
    auto it = order_lookup.find(order_id);
    if (it == order_lookup.end())
        return false;

    it->second->is_active = false;
    order_lookup.erase(it);
    return true;
}

BBO OrderBook::getBBO() const
{
    BBO bbo{};
    if (!bids.empty())
    {
        bbo.best_bid_price = bids.back()->price;
        bbo.best_bid_qty = bids.back()->quantity;
    }
    if (!asks.empty())
    {
        bbo.best_ask_price = asks.back()->price;
        bbo.best_ask_qty = asks.back()->quantity;
    }
    return bbo;
}

void OrderBook::reset()
{
    bids.clear();
    asks.clear();
    order_lookup.clear();
}
