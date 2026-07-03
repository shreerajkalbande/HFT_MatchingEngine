#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../BitmapOrderBook.h"
#include "../MatchingEngine.h"
#include "../OrderArena.h"
#include "../OrderBook.h"

namespace
{
int tests_passed = 0;
int tests_failed = 0;

#define TEST(name) static void name()
#define RUN_TEST(name) do { \
    std::cout << "  " #name "... "; \
    try { name(); std::cout << "PASSED\n"; ++tests_passed; } \
    catch (...) { std::cout << "FAILED\n"; ++tests_failed; } \
} while (0)

#define ASSERT_EQ(a, b) do { const auto actual = (a); const auto expected = (b); \
    if (actual != expected) { \
        std::cerr << "ASSERT_EQ failed: " << actual << " != " << expected \
                  << " at line " << __LINE__ << "\n"; throw 1; } } while (0)

#define ASSERT_TRUE(x) do { if (!(x)) { \
    std::cerr << "ASSERT_TRUE failed at line " << __LINE__ << "\n"; throw 1; } } while (0)

#define ASSERT_FALSE(x) do { if (x) { \
    std::cerr << "ASSERT_FALSE failed at line " << __LINE__ << "\n"; throw 1; } } while (0)

#define ASSERT_NULL(x) do { if ((x) != nullptr) { \
    std::cerr << "ASSERT_NULL failed at line " << __LINE__ << "\n"; throw 1; } } while (0)

#define ASSERT_NOT_NULL(x) do { if ((x) == nullptr) { \
    std::cerr << "ASSERT_NOT_NULL failed at line " << __LINE__ << "\n"; throw 1; } } while (0)

TEST(test_arena_allocation_exhaustion_and_reset)
{
    OrderArena arena(2);
    Order* first = arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    Order* second = arena.allocateOrder(2, 101, 60, Side::ASK, 2000);

    ASSERT_NOT_NULL(first);
    ASSERT_NOT_NULL(second);
    ASSERT_EQ(first->id, 1UL);
    ASSERT_EQ(second->timestamp, 2000UL);
    ASSERT_EQ(arena.size(), 2UL);

    std::streambuf* old = std::cerr.rdbuf(nullptr);
    Order* overflow = arena.allocateOrder(3, 102, 70, Side::BID, 3000);
    std::cerr.rdbuf(old);
    ASSERT_NULL(overflow);

    arena.reset();
    ASSERT_EQ(arena.size(), 0UL);
    ASSERT_NOT_NULL(arena.allocateOrder(4, 103, 80, Side::ASK, 4000));
}

TEST(test_arena_reuses_deallocated_slot)
{
    OrderArena arena(1);
    Order* first = arena.allocateOrder(1, 100, 10, Side::BID, 1);
    ASSERT_NOT_NULL(first);
    ASSERT_TRUE(arena.deallocateOrder(first));
    Order* second = arena.allocateOrder(2, 101, 20, Side::ASK, 2);
    ASSERT_EQ(second, first);
    ASSERT_EQ(second->id, 2UL);
    ASSERT_EQ(arena.size(), 1UL);
}

template <typename Book>
void verifyBookBehavior()
{
    OrderArena arena(16);
    Book book(16);

    ASSERT_TRUE(book.bidsEmpty());
    ASSERT_TRUE(book.asksEmpty());
    ASSERT_NULL(book.bestBid());
    ASSERT_NULL(book.bestAsk());

    Order* low_bid = arena.allocateOrder(1, 100, 40, Side::BID, 1);
    Order* old_best_bid = arena.allocateOrder(2, 102, 30, Side::BID, 2);
    Order* new_best_bid = arena.allocateOrder(3, 102, 20, Side::BID, 3);
    Order* high_ask = arena.allocateOrder(4, 108, 60, Side::ASK, 4);
    Order* best_ask = arena.allocateOrder(5, 105, 70, Side::ASK, 5);

    ASSERT_TRUE(book.insertBid(low_bid));
    ASSERT_TRUE(book.insertBid(old_best_bid));
    ASSERT_TRUE(book.insertBid(new_best_bid));
    ASSERT_TRUE(book.insertAsk(high_ask));
    ASSERT_TRUE(book.insertAsk(best_ask));

    ASSERT_EQ(book.bestBid()->id, 2UL);
    ASSERT_EQ(book.bestAsk()->id, 5UL);
    ASSERT_EQ(book.bidDepth(), 3UL);
    ASSERT_EQ(book.askDepth(), 2UL);
    ASSERT_EQ(book.bidLevels(), 2UL);
    ASSERT_EQ(book.askLevels(), 2UL);

    BBO bbo = book.getBBO();
    ASSERT_EQ(bbo.best_bid_price, 102U);
    ASSERT_EQ(bbo.best_bid_qty, 50UL);
    ASSERT_EQ(bbo.best_ask_price, 105U);
    ASSERT_EQ(bbo.best_ask_qty, 70UL);

    book.fillBestBid(30);
    ASSERT_FALSE(old_best_bid->is_active);
    ASSERT_EQ(book.bestBid()->id, 3UL);
    ASSERT_EQ(book.getBBO().best_bid_qty, 20UL);

    ASSERT_TRUE(book.cancelOrder(3));
    ASSERT_EQ(book.bestBid()->id, 1UL);
    ASSERT_EQ(book.getBBO().best_bid_price, 100U);
    ASSERT_FALSE(book.cancelOrder(999));

    book.reset();
    ASSERT_FALSE(low_bid->is_active);
    ASSERT_FALSE(high_ask->is_active);
    ASSERT_FALSE(best_ask->is_active);
    ASSERT_EQ(book.totalOrders(), 0UL);
    ASSERT_EQ(book.bidLevels(), 0UL);
    ASSERT_EQ(book.askLevels(), 0UL);
}

TEST(test_map_order_book_conformance)
{
    verifyBookBehavior<MapOrderBook>();
}

TEST(test_bitmap_order_book_conformance)
{
    verifyBookBehavior<BitmapOrderBook>();
}

TEST(test_bitmap_crosses_word_and_summary_boundaries)
{
    OrderArena arena(16);
    BitmapOrderBook book;
    const uint32_t prices[] = {1, 64, 65, 4096, 4097, 10'000, 20'000};

    for (size_t i = 0; i < 7; ++i)
    {
        Order* order = arena.allocateOrder(i + 1, prices[i], 10, Side::ASK, i);
        ASSERT_TRUE(book.insertAsk(order));
    }

    for (size_t i = 0; i < 7; ++i)
    {
        ASSERT_EQ(book.bestAsk()->price, prices[i]);
        ASSERT_TRUE(book.cancelOrder(i + 1));
    }

    ASSERT_TRUE(book.asksEmpty());

    for (size_t i = 0; i < 7; ++i)
    {
        Order* order = arena.allocateOrder(
            i + 8, prices[i], 10, Side::BID, i + 8);
        ASSERT_TRUE(book.insertBid(order));
    }

    for (size_t i = 7; i-- > 0;)
    {
        ASSERT_EQ(book.bestBid()->price, prices[i]);
        ASSERT_TRUE(book.cancelOrder(i + 8));
    }

    ASSERT_TRUE(book.bidsEmpty());
}

TEST(test_bitmap_rejects_prices_outside_dense_domain)
{
    OrderArena arena(3);
    BitmapOrderBook book;
    Order* zero = arena.allocateOrder(1, 0, 10, Side::BID, 1);
    Order* too_high = arena.allocateOrder(
        2, BitmapOrderBook::MAX_PRICE + 1, 10, Side::ASK, 2);

    ASSERT_FALSE(book.insertBid(zero));
    ASSERT_FALSE(book.insertAsk(too_high));
    ASSERT_EQ(book.totalOrders(), 0UL);

    BitmapMatchingEngine engine;
    Order* engine_order = arena.allocateOrder(
        3, BitmapOrderBook::MAX_PRICE + 1, 10, Side::BID, 3);
    ASSERT_FALSE(engine.processOrder(engine_order));
    ASSERT_EQ(engine.totalOrders(), 0UL);
}

struct ExecutionLog
{
    std::vector<Execution> values;
};

void recordExecution(const Execution& execution, void* context)
{
    static_cast<ExecutionLog*>(context)->values.push_back(execution);
}

template <typename Engine>
void verifyEngineBehavior()
{
    OrderArena arena(20);
    ExecutionLog log;
    Engine engine(20, recordExecution, &log);

    Order* ask_old = arena.allocateOrder(1, 100, 30, Side::ASK, 1);
    Order* ask_new = arena.allocateOrder(2, 100, 40, Side::ASK, 2);
    Order* ask_next = arena.allocateOrder(3, 101, 50, Side::ASK, 3);
    ASSERT_TRUE(engine.processOrder(ask_old));
    ASSERT_TRUE(engine.processOrder(ask_new));
    ASSERT_TRUE(engine.processOrder(ask_next));

    Order* bid = arena.allocateOrder(4, 101, 90, Side::BID, 4);
    ASSERT_TRUE(engine.processOrder(bid));

    ASSERT_EQ(ask_old->quantity, 0U);
    ASSERT_EQ(ask_new->quantity, 0U);
    ASSERT_EQ(ask_next->quantity, 30U);
    ASSERT_EQ(bid->quantity, 0U);
    ASSERT_EQ(log.values.size(), 3UL);
    ASSERT_EQ(log.values[0].maker_order_id, 1UL);
    ASSERT_EQ(log.values[1].maker_order_id, 2UL);
    ASSERT_EQ(log.values[2].maker_order_id, 3UL);
    ASSERT_EQ(log.values[0].price, 100U);
    ASSERT_EQ(log.values[2].price, 101U);
    ASSERT_EQ(engine.getBBO().best_ask_qty, 30UL);

    Order* duplicate = arena.allocateOrder(3, 101, 10, Side::BID, 5);
    ASSERT_FALSE(engine.processOrder(duplicate));
    ASSERT_TRUE(engine.cancelOrder(3));
    ASSERT_EQ(engine.totalOrders(), 0UL);

    Order* bid_old = arena.allocateOrder(5, 99, 25, Side::BID, 6);
    Order* bid_new = arena.allocateOrder(6, 99, 25, Side::BID, 7);
    engine.processOrder(bid_old);
    engine.processOrder(bid_new);
    ASSERT_TRUE(engine.cancelOrder(5));

    Order* ask = arena.allocateOrder(7, 99, 25, Side::ASK, 8);
    ASSERT_TRUE(engine.processOrder(ask));
    ASSERT_EQ(bid_old->quantity, 25U);
    ASSERT_FALSE(bid_old->is_active);
    ASSERT_EQ(bid_new->quantity, 0U);
}

TEST(test_map_matching_engine_conformance)
{
    verifyEngineBehavior<MapMatchingEngine>();
}

TEST(test_bitmap_matching_engine_conformance)
{
    verifyEngineBehavior<BitmapMatchingEngine>();
}

template <typename Engine>
std::vector<BBO> runDeterministicStream()
{
    struct Event
    {
        uint64_t id;
        uint32_t price;
        uint32_t quantity;
        Side side;
        bool cancel;
    };

    const Event events[] = {
        {1, 9900, 50, Side::BID, false},
        {2, 9901, 20, Side::BID, false},
        {3, 10'100, 40, Side::ASK, false},
        {4, 10'101, 30, Side::ASK, false},
        {2, 0, 0, Side::BID, true},
        {5, 10'100, 15, Side::BID, false},
        {6, 10'101, 40, Side::BID, false},
        {1, 0, 0, Side::BID, true},
        {7, 10'099, 25, Side::ASK, false},
        {8, 10'099, 25, Side::BID, false},
    };

    OrderArena arena(16);
    Engine engine(16);
    std::vector<BBO> states;

    for (const Event& event : events)
    {
        if (event.cancel)
        {
            engine.cancelOrder(event.id);
        }
        else
        {
            Order* order = arena.allocateOrder(
                event.id, event.price, event.quantity, event.side, event.id);
            engine.processOrder(order);
        }
        states.push_back(engine.getBBO());
    }

    return states;
}

TEST(test_backends_produce_identical_bbo_stream)
{
    const std::vector<BBO> map_states =
        runDeterministicStream<MapMatchingEngine>();
    const std::vector<BBO> bitmap_states =
        runDeterministicStream<BitmapMatchingEngine>();

    ASSERT_EQ(map_states.size(), bitmap_states.size());
    for (size_t i = 0; i < map_states.size(); ++i)
    {
        ASSERT_EQ(map_states[i].best_bid_price, bitmap_states[i].best_bid_price);
        ASSERT_EQ(map_states[i].best_bid_qty, bitmap_states[i].best_bid_qty);
        ASSERT_EQ(map_states[i].best_ask_price, bitmap_states[i].best_ask_price);
        ASSERT_EQ(map_states[i].best_ask_qty, bitmap_states[i].best_ask_qty);
    }
}
} // namespace

int main()
{
    std::cout << "\n=== Matching Engine Test Suite ===\n\n";

    std::cout << "[Arena]\n";
    RUN_TEST(test_arena_allocation_exhaustion_and_reset);
    RUN_TEST(test_arena_reuses_deallocated_slot);

    std::cout << "\n[Order-book backends]\n";
    RUN_TEST(test_map_order_book_conformance);
    RUN_TEST(test_bitmap_order_book_conformance);
    RUN_TEST(test_bitmap_crosses_word_and_summary_boundaries);
    RUN_TEST(test_bitmap_rejects_prices_outside_dense_domain);

    std::cout << "\n[Matching engine]\n";
    RUN_TEST(test_map_matching_engine_conformance);
    RUN_TEST(test_bitmap_matching_engine_conformance);
    RUN_TEST(test_backends_produce_identical_bbo_stream);

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===\n\n";
    return tests_failed == 0 ? 0 : 1;
}
