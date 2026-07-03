#pragma once

#include <algorithm>

template <typename Book>
BasicMatchingEngine<Book>::BasicMatchingEngine(size_t expected_resting_orders,
                                                ExecutionHandler handler,
                                                void* context)
    : book(expected_resting_orders),
      execution_handler(handler),
      execution_context(context),
      arena(nullptr)
{
}

template <typename Book>
BasicMatchingEngine<Book>::BasicMatchingEngine(OrderArena& order_arena,
                                                size_t expected_resting_orders,
                                                ExecutionHandler handler,
                                                void* context)
    : BasicMatchingEngine(expected_resting_orders, handler, context)
{
    arena = &order_arena;
    book.setArena(arena);
}

template <typename Book>
void BasicMatchingEngine<Book>::setExecutionHandler(ExecutionHandler handler,
                                                     void* context)
{
    execution_handler = handler;
    execution_context = context;
}

template <typename Book>
void BasicMatchingEngine<Book>::publishExecution(const Execution& execution) const
{
    if (execution_handler)
        execution_handler(execution, execution_context);
}

template <typename Book>
void BasicMatchingEngine<Book>::releaseIncoming(Order* order) const
{
    if (!order)
        return;
    if (arena)
        arena->deallocateOrder(order);
    else
        order->is_active = false;
}

template <typename Book>
bool BasicMatchingEngine<Book>::processOrder(Order* incoming)
{
    if (!incoming || !incoming->is_active ||
        !book.acceptsPrice(incoming->price) ||
        incoming->quantity == 0 ||
        (incoming->side != Side::BID && incoming->side != Side::ASK) ||
        book.contains(incoming->id))
    {
        releaseIncoming(incoming);
        return false;
    }

    if (incoming->side == Side::BID)
    {
        while (incoming->quantity > 0)
        {
            Order* best_ask = book.bestAsk();
            if (!best_ask || incoming->price < best_ask->price)
                break;

            const uint32_t traded_qty =
                std::min(incoming->quantity, best_ask->quantity);
            const Execution execution{
                best_ask->id, incoming->id, best_ask->price, traded_qty};

            incoming->quantity -= traded_qty;
            book.fillBestAsk(traded_qty);
            publishExecution(execution);
        }

        if (incoming->quantity > 0)
        {
            const bool ok = book.insertBid(incoming);
            if (!ok)
                releaseIncoming(incoming);
            return ok;
        }
    }
    else
    {
        while (incoming->quantity > 0)
        {
            Order* best_bid = book.bestBid();
            if (!best_bid || incoming->price > best_bid->price)
                break;

            const uint32_t traded_qty =
                std::min(incoming->quantity, best_bid->quantity);
            const Execution execution{
                best_bid->id, incoming->id, best_bid->price, traded_qty};

            incoming->quantity -= traded_qty;
            book.fillBestBid(traded_qty);
            publishExecution(execution);
        }

        if (incoming->quantity > 0)
        {
            const bool ok = book.insertAsk(incoming);
            if (!ok)
                releaseIncoming(incoming);
            return ok;
        }
    }

    releaseIncoming(incoming);
    return true;
}

template <typename Book>
bool BasicMatchingEngine<Book>::cancelOrder(uint64_t order_id)
{
    return book.cancelOrder(order_id);
}
