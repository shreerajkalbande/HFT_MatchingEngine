#include "BitmapOrderBook.h"

#include <algorithm>
#include <cassert>

namespace
{
constexpr size_t BITS_PER_WORD = 64;

size_t wordsForBits(size_t bits)
{
    return (bits + BITS_PER_WORD - 1) / BITS_PER_WORD;
}

unsigned lowestSetBit(uint64_t word)
{
    assert(word != 0);
    return static_cast<unsigned>(__builtin_ctzll(word));
}

unsigned highestSetBit(uint64_t word)
{
    assert(word != 0);
    return 63U - static_cast<unsigned>(__builtin_clzll(word));
}
} // namespace

BitmapOrderBook::HierarchicalBitmap::HierarchicalBitmap(size_t bits)
    : bit_count(bits)
{
    assert(bits > 0);
    size_t words = wordsForBits(bits);
    hierarchy.emplace_back(words, 0);

    while (words > 1)
    {
        words = wordsForBits(words);
        hierarchy.emplace_back(words, 0);
    }
}

void BitmapOrderBook::HierarchicalBitmap::set(size_t bit_index)
{
    if (bit_index >= bit_count)
        return;
    size_t index = bit_index;

    for (auto& words : hierarchy)
    {
        const size_t word_index = index / BITS_PER_WORD;
        const uint64_t mask = uint64_t{1} << (index % BITS_PER_WORD);
        const uint64_t old_word = words[word_index];
        words[word_index] = old_word | mask;

        if (old_word != 0)
            break;

        index = word_index;
    }
}

void BitmapOrderBook::HierarchicalBitmap::clear(size_t bit_index)
{
    if (bit_index >= bit_count)
        return;
    size_t index = bit_index;

    for (auto& words : hierarchy)
    {
        const size_t word_index = index / BITS_PER_WORD;
        const uint64_t mask = uint64_t{1} << (index % BITS_PER_WORD);
        words[word_index] &= ~mask;

        if (words[word_index] != 0)
            break;

        index = word_index;
    }
}

bool BitmapOrderBook::HierarchicalBitmap::empty() const
{
    return hierarchy.back()[0] == 0;
}

size_t BitmapOrderBook::HierarchicalBitmap::firstSet() const
{
    assert(!empty());
    size_t index = lowestSetBit(hierarchy.back()[0]);

    for (size_t level = hierarchy.size() - 1; level-- > 0;)
    {
        const uint64_t word = hierarchy[level][index];
        index = index * BITS_PER_WORD + lowestSetBit(word);
    }

    assert(index < bit_count);
    return index;
}

size_t BitmapOrderBook::HierarchicalBitmap::lastSet() const
{
    assert(!empty());
    size_t index = highestSetBit(hierarchy.back()[0]);

    for (size_t level = hierarchy.size() - 1; level-- > 0;)
    {
        const uint64_t word = hierarchy[level][index];
        index = index * BITS_PER_WORD + highestSetBit(word);
    }

    assert(index < bit_count);
    return index;
}

void BitmapOrderBook::HierarchicalBitmap::reset()
{
    for (auto& words : hierarchy)
        std::fill(words.begin(), words.end(), 0);
}

size_t BitmapOrderBook::HierarchicalBitmap::storageBytes() const
{
    size_t bytes = 0;
    for (const auto& words : hierarchy)
        bytes += words.capacity() * sizeof(uint64_t);
    return bytes;
}

BitmapOrderBook::BitmapOrderBook(size_t expected_resting_orders)
    : bid_levels(LEVEL_COUNT),
      ask_levels(LEVEL_COUNT),
      occupied_bids(LEVEL_COUNT),
      occupied_asks(LEVEL_COUNT)
{
    if (expected_resting_orders > 0)
        order_lookup.reserve(expected_resting_orders);
}

void BitmapOrderBook::releaseOrder(Order* order)
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

bool BitmapOrderBook::validPrice(uint32_t price)
{
    return price >= MIN_PRICE && price <= MAX_PRICE;
}

size_t BitmapOrderBook::priceIndex(uint32_t price)
{
    assert(validPrice(price));
    return static_cast<size_t>(price - MIN_PRICE);
}

uint32_t BitmapOrderBook::indexPrice(size_t index)
{
    assert(index < LEVEL_COUNT);
    return MIN_PRICE + static_cast<uint32_t>(index);
}

bool BitmapOrderBook::insert(Order* order,
                             Side expected_side,
                             std::vector<PriceLevel>& levels,
                             HierarchicalBitmap& occupied,
                             size_t& order_count,
                             size_t& level_count)
{
    if (!order || order->side != expected_side || !order->is_active ||
        !validPrice(order->price) || order->quantity == 0 || contains(order->id))
    {
        return false;
    }

    auto [lookup_it, inserted] = order_lookup.emplace(order->id, order);
    if (!inserted)
        return false;
    (void)lookup_it;

    const size_t index = priceIndex(order->price);
    PriceLevel& level = levels[index];
    if (level.order_count == 0)
    {
        occupied.set(index);
        ++level_count;
    }

    appendOrder(level, order);
    ++order_count;
    return true;
}

bool BitmapOrderBook::insertBid(Order* order)
{
    return insert(order, Side::BID, bid_levels, occupied_bids,
                  bid_order_count, bid_level_count);
}

bool BitmapOrderBook::insertAsk(Order* order)
{
    return insert(order, Side::ASK, ask_levels, occupied_asks,
                  ask_order_count, ask_level_count);
}

Order* BitmapOrderBook::bestBid() const
{
    if (occupied_bids.empty())
        return nullptr;
    return bid_levels[occupied_bids.lastSet()].head;
}

Order* BitmapOrderBook::bestAsk() const
{
    if (occupied_asks.empty())
        return nullptr;
    return ask_levels[occupied_asks.firstSet()].head;
}

void BitmapOrderBook::fillBest(std::vector<PriceLevel>& levels,
                               HierarchicalBitmap& occupied,
                               size_t& order_count,
                               size_t& level_count,
                               uint32_t quantity,
                               bool highest)
{
    assert(!occupied.empty());
    const size_t index = highest ? occupied.lastSet() : occupied.firstSet();
    PriceLevel& level = levels[index];
    Order* order = level.head;
    assert(order && quantity > 0 && quantity <= order->quantity);

    order->quantity -= quantity;
    level.total_quantity -= quantity;

    if (order->quantity == 0)
    {
        unlinkOrder(level, order);
        order_lookup.erase(order->id);
        --order_count;

        if (level.order_count == 0)
        {
            occupied.clear(index);
            --level_count;
        }
        releaseOrder(order);
    }
}

void BitmapOrderBook::fillBestBid(uint32_t quantity)
{
    fillBest(bid_levels, occupied_bids, bid_order_count, bid_level_count,
             quantity, true);
}

void BitmapOrderBook::fillBestAsk(uint32_t quantity)
{
    fillBest(ask_levels, occupied_asks, ask_order_count, ask_level_count,
             quantity, false);
}

bool BitmapOrderBook::cancelOrder(uint64_t order_id)
{
    auto lookup_it = order_lookup.find(order_id);
    if (lookup_it == order_lookup.end())
        return false;

    Order* order = lookup_it->second;
    const size_t index = priceIndex(order->price);
    std::vector<PriceLevel>& levels =
        order->side == Side::BID ? bid_levels : ask_levels;
    HierarchicalBitmap& occupied =
        order->side == Side::BID ? occupied_bids : occupied_asks;
    size_t& order_count =
        order->side == Side::BID ? bid_order_count : ask_order_count;
    size_t& level_count =
        order->side == Side::BID ? bid_level_count : ask_level_count;

    PriceLevel& level = levels[index];
    level.total_quantity -= order->quantity;
    unlinkOrder(level, order);
    --order_count;

    if (level.order_count == 0)
    {
        occupied.clear(index);
        --level_count;
    }

    order_lookup.erase(lookup_it);
    releaseOrder(order);
    return true;
}

bool BitmapOrderBook::contains(uint64_t order_id) const
{
    return order_lookup.find(order_id) != order_lookup.end();
}

BBO BitmapOrderBook::getBBO() const
{
    BBO bbo{};
    if (!occupied_bids.empty())
    {
        const size_t index = occupied_bids.lastSet();
        bbo.best_bid_price = indexPrice(index);
        bbo.best_bid_qty = bid_levels[index].total_quantity;
    }
    if (!occupied_asks.empty())
    {
        const size_t index = occupied_asks.firstSet();
        bbo.best_ask_price = indexPrice(index);
        bbo.best_ask_qty = ask_levels[index].total_quantity;
    }
    return bbo;
}

size_t BitmapOrderBook::denseStorageBytes() const
{
    return (bid_levels.capacity() + ask_levels.capacity()) * sizeof(PriceLevel);
}

size_t BitmapOrderBook::bitmapStorageBytes() const
{
    return occupied_bids.storageBytes() + occupied_asks.storageBytes();
}

void BitmapOrderBook::reset()
{
    for (auto& [id, order] : order_lookup)
    {
        (void)id;
        releaseOrder(order);
    }

    for (auto& level : bid_levels)
        level = {};
    for (auto& level : ask_levels)
        level = {};

    occupied_bids.reset();
    occupied_asks.reset();
    order_lookup.clear();
    bid_order_count = 0;
    ask_order_count = 0;
    bid_level_count = 0;
    ask_level_count = 0;
}
