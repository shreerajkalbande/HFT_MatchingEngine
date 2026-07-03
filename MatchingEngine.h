#pragma once

#include <cstddef>
#include <cstdint>

#include "BitmapOrderBook.h"
#include "Order.h"
#include "OrderArena.h"
#include "OrderBook.h"

struct Execution
{
    uint64_t maker_order_id;
    uint64_t taker_order_id;
    uint32_t price;
    uint32_t quantity;
};

using ExecutionHandler = void (*)(const Execution&, void* context);

template <typename Book>
class BasicMatchingEngine
{
private:
    Book book;
    ExecutionHandler execution_handler;
    void* execution_context;
    OrderArena* arena;

    void publishExecution(const Execution& execution) const;
    void releaseIncoming(Order* order) const;

public:
    explicit BasicMatchingEngine(size_t expected_resting_orders = 0,
                                 ExecutionHandler handler = nullptr,
                                 void* context = nullptr);
    explicit BasicMatchingEngine(OrderArena& order_arena,
                                 size_t expected_resting_orders = 0,
                                 ExecutionHandler handler = nullptr,
                                 void* context = nullptr);

    // Returns false for invalid input or an ID that is already resting.
    bool processOrder(Order* incoming);
    bool cancelOrder(uint64_t order_id);

    void setExecutionHandler(ExecutionHandler handler, void* context = nullptr);

    BBO getBBO() const { return book.getBBO(); }
    size_t bidDepth() const { return book.bidDepth(); }
    size_t askDepth() const { return book.askDepth(); }
    size_t bidLevels() const { return book.bidLevels(); }
    size_t askLevels() const { return book.askLevels(); }
    size_t totalOrders() const { return book.totalOrders(); }

    void reset() { book.reset(); }
};

using MapMatchingEngine = BasicMatchingEngine<MapOrderBook>;
using BitmapMatchingEngine = BasicMatchingEngine<BitmapOrderBook>;
using MatchingEngine = MapMatchingEngine;

#include "MatchingEngine.tpp"
