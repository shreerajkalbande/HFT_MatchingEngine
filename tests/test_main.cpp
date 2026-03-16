#include <cassert>
#include <iostream>
#include <cstring>

#include "../OrderArena.h"
#include "../OrderBook.h"
#include "../MatchingEngine.h"
#include "../SPSCRingBuffer.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name()
#define RUN_TEST(name) do { \
    std::cout << "  " #name "... "; \
    try { name(); std::cout << "PASSED\n"; tests_passed++; } \
    catch (...) { std::cout << "FAILED\n"; tests_failed++; } \
} while(0)

#define ASSERT_EQ(a, b) do { if ((a) != (b)) { \
    std::cerr << "ASSERT_EQ failed: " << (a) << " != " << (b) \
              << " at line " << __LINE__ << "\n"; throw 1; } } while(0)

#define ASSERT_TRUE(x)  do { if (!(x)) { \
    std::cerr << "ASSERT_TRUE failed at line " << __LINE__ << "\n"; throw 1; } } while(0)

#define ASSERT_FALSE(x) do { if ((x)) { \
    std::cerr << "ASSERT_FALSE failed at line " << __LINE__ << "\n"; throw 1; } } while(0)

#define ASSERT_NULL(x) do { if ((x) != nullptr) { \
    std::cerr << "ASSERT_NULL failed at line " << __LINE__ << "\n"; throw 1; } } while(0)

#define ASSERT_NOT_NULL(x) do { if ((x) == nullptr) { \
    std::cerr << "ASSERT_NOT_NULL failed at line " << __LINE__ << "\n"; throw 1; } } while(0)

// ========================== Arena Tests ==========================

TEST(test_arena_basic_allocation)
{
    OrderArena arena(100);
    Order* o = arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    ASSERT_NOT_NULL(o);
    ASSERT_EQ(o->id, 1UL);
    ASSERT_EQ(o->price, 100U);
    ASSERT_EQ(o->quantity, 50U);
    ASSERT_TRUE(o->side == Side::BID);
    ASSERT_EQ(o->timestamp, 1000UL);
    ASSERT_TRUE(o->is_active);
}

TEST(test_arena_sequential_allocation)
{
    OrderArena arena(100);
    Order* o1 = arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    Order* o2 = arena.allocateOrder(2, 101, 60, Side::ASK, 2000);
    ASSERT_NOT_NULL(o1);
    ASSERT_NOT_NULL(o2);
    ASSERT_TRUE(o1 != o2);
    ASSERT_EQ(arena.size(), 2UL);
}

TEST(test_arena_exhaustion)
{
    OrderArena arena(2);
    Order* o1 = arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    Order* o2 = arena.allocateOrder(2, 101, 60, Side::ASK, 2000);
    ASSERT_NOT_NULL(o1);
    ASSERT_NOT_NULL(o2);

    // Suppress cerr output for this test
    std::streambuf* old = std::cerr.rdbuf(nullptr);
    Order* o3 = arena.allocateOrder(3, 102, 70, Side::BID, 3000);
    std::cerr.rdbuf(old);

    ASSERT_NULL(o3);
    ASSERT_EQ(arena.size(), 2UL);
}

TEST(test_arena_reset)
{
    OrderArena arena(2);
    arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    arena.allocateOrder(2, 101, 60, Side::ASK, 2000);
    ASSERT_EQ(arena.size(), 2UL);

    arena.reset();
    ASSERT_EQ(arena.size(), 0UL);

    Order* o = arena.allocateOrder(3, 102, 70, Side::BID, 3000);
    ASSERT_NOT_NULL(o);
    ASSERT_EQ(o->id, 3UL);
}

// ========================== OrderBook Tests ==========================

TEST(test_orderbook_empty)
{
    OrderBook book;
    ASSERT_TRUE(book.bidsEmpty());
    ASSERT_TRUE(book.asksEmpty());
    ASSERT_NULL(book.bestBid());
    ASSERT_NULL(book.bestAsk());
    ASSERT_EQ(book.bidDepth(), 0UL);
    ASSERT_EQ(book.askDepth(), 0UL);
}

TEST(test_orderbook_insert_bid)
{
    OrderArena arena(10);
    OrderBook book;
    Order* o = arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    book.insertBid(o);

    ASSERT_FALSE(book.bidsEmpty());
    ASSERT_EQ(book.bestBid()->id, 1UL);
    ASSERT_EQ(book.bidDepth(), 1UL);
}

TEST(test_orderbook_insert_ask)
{
    OrderArena arena(10);
    OrderBook book;
    Order* o = arena.allocateOrder(1, 105, 30, Side::ASK, 1000);
    book.insertAsk(o);

    ASSERT_FALSE(book.asksEmpty());
    ASSERT_EQ(book.bestAsk()->id, 1UL);
    ASSERT_EQ(book.askDepth(), 1UL);
}

TEST(test_orderbook_price_priority_bids)
{
    OrderArena arena(10);
    OrderBook book;
    Order* low  = arena.allocateOrder(1, 95,  50, Side::BID, 1000);
    Order* high = arena.allocateOrder(2, 100, 50, Side::BID, 2000);
    Order* mid  = arena.allocateOrder(3, 97,  50, Side::BID, 3000);

    book.insertBid(low);
    book.insertBid(high);
    book.insertBid(mid);

    // Best bid should be highest price
    ASSERT_EQ(book.bestBid()->id, 2UL);
    ASSERT_EQ(book.bestBid()->price, 100U);
}

TEST(test_orderbook_price_priority_asks)
{
    OrderArena arena(10);
    OrderBook book;
    Order* high = arena.allocateOrder(1, 110, 50, Side::ASK, 1000);
    Order* low  = arena.allocateOrder(2, 100, 50, Side::ASK, 2000);
    Order* mid  = arena.allocateOrder(3, 105, 50, Side::ASK, 3000);

    book.insertAsk(high);
    book.insertAsk(low);
    book.insertAsk(mid);

    // Best ask should be lowest price
    ASSERT_EQ(book.bestAsk()->id, 2UL);
    ASSERT_EQ(book.bestAsk()->price, 100U);
}

TEST(test_orderbook_bbo)
{
    OrderArena arena(10);
    OrderBook book;
    Order* bid = arena.allocateOrder(1, 99, 100, Side::BID, 1000);
    Order* ask = arena.allocateOrder(2, 101, 200, Side::ASK, 2000);
    book.insertBid(bid);
    book.insertAsk(ask);

    BBO bbo = book.getBBO();
    ASSERT_EQ(bbo.best_bid_price, 99U);
    ASSERT_EQ(bbo.best_bid_qty, 100U);
    ASSERT_EQ(bbo.best_ask_price, 101U);
    ASSERT_EQ(bbo.best_ask_qty, 200U);
}

TEST(test_orderbook_cancel)
{
    OrderArena arena(10);
    OrderBook book;
    Order* o = arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    book.insertBid(o);

    ASSERT_TRUE(book.cancelOrder(1));
    ASSERT_FALSE(o->is_active);
}

TEST(test_orderbook_cancel_nonexistent)
{
    OrderBook book;
    ASSERT_FALSE(book.cancelOrder(999));
}

// ========================== Matching Engine Tests ==========================

TEST(test_basic_match)
{
    OrderArena arena(10);
    MatchingEngine engine;

    Order* ask = arena.allocateOrder(1, 100, 50, Side::ASK, 1000);
    engine.processOrder(ask);

    Order* bid = arena.allocateOrder(2, 100, 50, Side::BID, 2000);
    engine.processOrder(bid);

    ASSERT_EQ(ask->quantity, 0U);
    ASSERT_EQ(bid->quantity, 0U);
    ASSERT_FALSE(ask->is_active);
    ASSERT_FALSE(bid->is_active);
}

TEST(test_no_match)
{
    OrderArena arena(10);
    MatchingEngine engine;

    Order* ask = arena.allocateOrder(1, 105, 50, Side::ASK, 1000);
    engine.processOrder(ask);

    Order* bid = arena.allocateOrder(2, 100, 50, Side::BID, 2000);
    engine.processOrder(bid);

    // Bid price < ask price, no match
    ASSERT_EQ(ask->quantity, 50U);
    ASSERT_EQ(bid->quantity, 50U);
    ASSERT_TRUE(ask->is_active);
    ASSERT_TRUE(bid->is_active);
    ASSERT_EQ(engine.bidDepth(), 1UL);
    ASSERT_EQ(engine.askDepth(), 1UL);
}

TEST(test_partial_fill)
{
    OrderArena arena(10);
    MatchingEngine engine;

    Order* ask = arena.allocateOrder(1, 100, 30, Side::ASK, 1000);
    engine.processOrder(ask);

    Order* bid = arena.allocateOrder(2, 100, 50, Side::BID, 2000);
    engine.processOrder(bid);

    // ask fully filled, bid has 20 remaining
    ASSERT_EQ(ask->quantity, 0U);
    ASSERT_FALSE(ask->is_active);
    ASSERT_EQ(bid->quantity, 20U);
    ASSERT_TRUE(bid->is_active);
    ASSERT_EQ(engine.bidDepth(), 1UL);
}

TEST(test_fifo_priority)
{
    OrderArena arena(10);
    MatchingEngine engine;

    // Two asks at same price, inserted in order (older first)
    Order* ask_old = arena.allocateOrder(1, 100, 50, Side::ASK, 1000);  // older
    Order* ask_new = arena.allocateOrder(2, 100, 50, Side::ASK, 2000);  // newer
    engine.processOrder(ask_old);
    engine.processOrder(ask_new);

    // Bid that matches exactly one ask
    Order* bid = arena.allocateOrder(3, 100, 50, Side::BID, 3000);
    engine.processOrder(bid);

    // FIFO: older ask must be filled first
    ASSERT_EQ(ask_old->quantity, 0U);
    ASSERT_FALSE(ask_old->is_active);
    ASSERT_EQ(ask_new->quantity, 50U);
    ASSERT_TRUE(ask_new->is_active);
    ASSERT_EQ(bid->quantity, 0U);
}

TEST(test_fifo_priority_bids)
{
    OrderArena arena(10);
    MatchingEngine engine;

    // Two bids at same price, inserted in order (older first)
    Order* bid_old = arena.allocateOrder(1, 100, 50, Side::BID, 1000);
    Order* bid_new = arena.allocateOrder(2, 100, 50, Side::BID, 2000);
    engine.processOrder(bid_old);
    engine.processOrder(bid_new);

    // Ask that matches exactly one bid
    Order* ask = arena.allocateOrder(3, 100, 50, Side::ASK, 3000);
    engine.processOrder(ask);

    // FIFO: older bid must be filled first
    ASSERT_EQ(bid_old->quantity, 0U);
    ASSERT_FALSE(bid_old->is_active);
    ASSERT_EQ(bid_new->quantity, 50U);
    ASSERT_TRUE(bid_new->is_active);
    ASSERT_EQ(ask->quantity, 0U);
}

TEST(test_multi_level_sweep)
{
    OrderArena arena(10);
    MatchingEngine engine;

    // Three asks at different prices
    Order* ask1 = arena.allocateOrder(1, 100, 30, Side::ASK, 1000);
    Order* ask2 = arena.allocateOrder(2, 101, 30, Side::ASK, 2000);
    Order* ask3 = arena.allocateOrder(3, 102, 30, Side::ASK, 3000);
    engine.processOrder(ask1);
    engine.processOrder(ask2);
    engine.processOrder(ask3);

    // Aggressive bid sweeps all levels
    Order* bid = arena.allocateOrder(4, 105, 70, Side::BID, 4000);
    engine.processOrder(bid);

    // Should fill ask1 (30@100) and ask2 (30@101), partial ask3 (10@102)
    ASSERT_EQ(ask1->quantity, 0U);
    ASSERT_EQ(ask2->quantity, 0U);
    ASSERT_EQ(ask3->quantity, 20U);
    ASSERT_EQ(bid->quantity, 0U);
}

TEST(test_cancel_then_match)
{
    OrderArena arena(10);
    MatchingEngine engine;

    // Two asks at same price
    Order* ask1 = arena.allocateOrder(1, 100, 50, Side::ASK, 1000);
    Order* ask2 = arena.allocateOrder(2, 100, 50, Side::ASK, 2000);
    engine.processOrder(ask1);
    engine.processOrder(ask2);

    // Cancel the older ask
    engine.cancelOrder(1);

    // Bid should match ask2 (ask1 was cancelled)
    Order* bid = arena.allocateOrder(3, 100, 50, Side::BID, 3000);
    engine.processOrder(bid);

    ASSERT_FALSE(ask1->is_active);
    ASSERT_EQ(ask1->quantity, 50U);  // cancelled, not filled
    ASSERT_EQ(ask2->quantity, 0U);
    ASSERT_EQ(bid->quantity, 0U);
}

// ========================== Ring Buffer Tests ==========================

TEST(test_ringbuffer_push_pop)
{
    SPSCRingBuffer<int, 8> rb;
    ASSERT_TRUE(rb.push(42));
    int val = 0;
    ASSERT_TRUE(rb.pop(val));
    ASSERT_EQ(val, 42);
}

TEST(test_ringbuffer_empty)
{
    SPSCRingBuffer<int, 8> rb;
    ASSERT_TRUE(rb.empty());
    int val = 0;
    ASSERT_FALSE(rb.pop(val));
}

TEST(test_ringbuffer_full)
{
    SPSCRingBuffer<int, 4> rb;  // capacity = 3 (one slot wasted)
    ASSERT_TRUE(rb.push(1));
    ASSERT_TRUE(rb.push(2));
    ASSERT_TRUE(rb.push(3));
    ASSERT_FALSE(rb.push(4));  // full
}

TEST(test_ringbuffer_wraparound)
{
    SPSCRingBuffer<int, 4> rb;
    int val = 0;

    // Fill and drain multiple times to wrap around
    for (int round = 0; round < 10; round++)
    {
        ASSERT_TRUE(rb.push(round * 10 + 1));
        ASSERT_TRUE(rb.push(round * 10 + 2));
        ASSERT_TRUE(rb.pop(val));
        ASSERT_EQ(val, round * 10 + 1);
        ASSERT_TRUE(rb.pop(val));
        ASSERT_EQ(val, round * 10 + 2);
    }
}

TEST(test_ringbuffer_size)
{
    SPSCRingBuffer<int, 8> rb;
    ASSERT_EQ(rb.size(), 0UL);
    rb.push(1);
    ASSERT_EQ(rb.size(), 1UL);
    rb.push(2);
    ASSERT_EQ(rb.size(), 2UL);
    int val;
    rb.pop(val);
    ASSERT_EQ(rb.size(), 1UL);
}

// ========================== Main ==========================

int main()
{
    std::cout << "\n=== HFT Matching Engine Test Suite ===\n\n";

    std::cout << "[Arena]\n";
    RUN_TEST(test_arena_basic_allocation);
    RUN_TEST(test_arena_sequential_allocation);
    RUN_TEST(test_arena_exhaustion);
    RUN_TEST(test_arena_reset);

    std::cout << "\n[OrderBook]\n";
    RUN_TEST(test_orderbook_empty);
    RUN_TEST(test_orderbook_insert_bid);
    RUN_TEST(test_orderbook_insert_ask);
    RUN_TEST(test_orderbook_price_priority_bids);
    RUN_TEST(test_orderbook_price_priority_asks);
    RUN_TEST(test_orderbook_bbo);
    RUN_TEST(test_orderbook_cancel);
    RUN_TEST(test_orderbook_cancel_nonexistent);

    std::cout << "\n[Matching Engine]\n";
    RUN_TEST(test_basic_match);
    RUN_TEST(test_no_match);
    RUN_TEST(test_partial_fill);
    RUN_TEST(test_fifo_priority);
    RUN_TEST(test_fifo_priority_bids);
    RUN_TEST(test_multi_level_sweep);
    RUN_TEST(test_cancel_then_match);

    std::cout << "\n[Ring Buffer]\n";
    RUN_TEST(test_ringbuffer_push_pop);
    RUN_TEST(test_ringbuffer_empty);
    RUN_TEST(test_ringbuffer_full);
    RUN_TEST(test_ringbuffer_wraparound);
    RUN_TEST(test_ringbuffer_size);

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===\n\n";

    return tests_failed > 0 ? 1 : 0;
}
