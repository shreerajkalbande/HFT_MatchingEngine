#include "MatchingEngine.h"
#include <iostream>
#include <algorithm>

void MatchingEngine::processOrder(Order* incoming)
{
    if (incoming->side == Side::BID)
    {
        // Match incoming bid against resting asks (lowest ask first)
        while (incoming->quantity > 0 && !book.asksEmpty())
        {
            Order* best_ask = book.bestAsk();

            // Skip cancelled orders (lazy deletion)
            if (!best_ask->is_active)
            {
                book.removeBestAsk();
                continue;
            }

            if (incoming->price >= best_ask->price)
            {
                uint32_t traded_qty = std::min(incoming->quantity, best_ask->quantity);
                std::cout << "[EXECUTION] " << traded_qty << " @ " << best_ask->price
                          << "  |  Buy #" << incoming->id
                          << " <-> Sell #" << best_ask->id << "\n";

                incoming->quantity -= traded_qty;
                best_ask->quantity -= traded_qty;

                if (best_ask->quantity == 0)
                {
                    best_ask->is_active = false;
                    book.removeBestAsk();
                }
            }
            else
            {
                break;  // no more matchable price levels
            }
        }

        if (incoming->quantity > 0)
        {
            book.insertBid(incoming);
        }
        else
        {
            incoming->is_active = false;
        }
    }
    else
    {
        // Match incoming ask against resting bids (highest bid first)
        while (incoming->quantity > 0 && !book.bidsEmpty())
        {
            Order* best_bid = book.bestBid();

            // Skip cancelled orders (lazy deletion)
            if (!best_bid->is_active)
            {
                book.removeBestBid();
                continue;
            }

            if (incoming->price <= best_bid->price)
            {
                uint32_t traded_qty = std::min(incoming->quantity, best_bid->quantity);
                std::cout << "[EXECUTION] " << traded_qty << " @ " << best_bid->price
                          << "  |  Sell #" << incoming->id
                          << " <-> Buy #" << best_bid->id << "\n";

                incoming->quantity -= traded_qty;
                best_bid->quantity -= traded_qty;

                if (best_bid->quantity == 0)
                {
                    best_bid->is_active = false;
                    book.removeBestBid();
                }
            }
            else
            {
                break;  // no more matchable price levels
            }
        }

        if (incoming->quantity > 0)
        {
            book.insertAsk(incoming);
        }
        else
        {
            incoming->is_active = false;
        }
    }
}

bool MatchingEngine::cancelOrder(uint64_t order_id)
{
    return book.cancelOrder(order_id);
}
