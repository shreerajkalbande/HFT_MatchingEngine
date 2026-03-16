#pragma once
#include "OrderBook.h"
#include "Order.h"

class MatchingEngine
{
private:
    OrderBook book;

public:
    void processOrder(Order* incoming);
    bool cancelOrder(uint64_t order_id);

    BBO getBBO() const { return book.getBBO(); }
    size_t bidDepth() const { return book.bidDepth(); }
    size_t askDepth() const { return book.askDepth(); }
    size_t totalOrders() const { return book.totalOrders(); }

    void reset() { book.reset(); }
};
