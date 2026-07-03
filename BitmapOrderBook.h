#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "BookTypes.h"
#include "Order.h"
#include "OrderArena.h"
#include "PriceLevel.h"

// Dense price levels plus a hierarchy of 64-bit occupancy summaries.
//
// hierarchy[0] has one bit per price. Every higher bit records whether one
// lower 64-bit word is non-zero. Best-price discovery follows one set bit per
// hierarchy level and never scans empty prices.
class BitmapOrderBook
{
public:
    static constexpr uint32_t MIN_PRICE = 1;
    static constexpr uint32_t MAX_PRICE = 20'000;
    static constexpr size_t LEVEL_COUNT =
        static_cast<size_t>(MAX_PRICE - MIN_PRICE) + 1;

private:
    class HierarchicalBitmap
    {
    private:
        std::vector<std::vector<uint64_t>> hierarchy;
        size_t bit_count;

    public:
        explicit HierarchicalBitmap(size_t bits);

        void set(size_t bit_index);
        void clear(size_t bit_index);
        bool empty() const;
        size_t firstSet() const;
        size_t lastSet() const;
        void reset();
        size_t storageBytes() const;
    };

    std::vector<PriceLevel> bid_levels;
    std::vector<PriceLevel> ask_levels;
    HierarchicalBitmap occupied_bids;
    HierarchicalBitmap occupied_asks;
    std::unordered_map<uint64_t, Order*> order_lookup;
    size_t bid_order_count = 0;
    size_t ask_order_count = 0;
    size_t bid_level_count = 0;
    size_t ask_level_count = 0;
    OrderArena* arena = nullptr;

    static bool validPrice(uint32_t price);
    static size_t priceIndex(uint32_t price);
    static uint32_t indexPrice(size_t index);
    void releaseOrder(Order* order);

    bool insert(Order* order,
                Side expected_side,
                std::vector<PriceLevel>& levels,
                HierarchicalBitmap& occupied,
                size_t& order_count,
                size_t& level_count);

    void fillBest(std::vector<PriceLevel>& levels,
                  HierarchicalBitmap& occupied,
                  size_t& order_count,
                  size_t& level_count,
                  uint32_t quantity,
                  bool highest);

public:
    explicit BitmapOrderBook(size_t expected_resting_orders = 0);
    void setArena(OrderArena* order_arena) { arena = order_arena; }

    bool insertBid(Order* order);
    bool insertAsk(Order* order);

    Order* bestBid() const;
    Order* bestAsk() const;
    void fillBestBid(uint32_t quantity);
    void fillBestAsk(uint32_t quantity);

    bool cancelOrder(uint64_t order_id);
    bool contains(uint64_t order_id) const;
    bool acceptsPrice(uint32_t price) const { return validPrice(price); }

    BBO getBBO() const;
    size_t bidDepth() const { return bid_order_count; }
    size_t askDepth() const { return ask_order_count; }
    size_t bidLevels() const { return bid_level_count; }
    size_t askLevels() const { return ask_level_count; }
    size_t totalOrders() const { return order_lookup.size(); }

    bool bidsEmpty() const { return occupied_bids.empty(); }
    bool asksEmpty() const { return occupied_asks.empty(); }

    size_t denseStorageBytes() const;
    size_t bitmapStorageBytes() const;
    void reset();
};
