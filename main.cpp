#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <random>
#include <atomic>

#include "OrderArena.h"
#include "MatchingEngine.h"
#include "SPSCRingBuffer.h"

static void printBBO(const MatchingEngine& engine)
{
    BBO bbo = engine.getBBO();
    std::cout << "  BBO: ";
    if (bbo.best_bid_price > 0)
        std::cout << bbo.best_bid_qty << "@" << bbo.best_bid_price;
    else
        std::cout << "---";
    std::cout << " | ";
    if (bbo.best_ask_price > 0)
        std::cout << bbo.best_ask_qty << "@" << bbo.best_ask_price;
    else
        std::cout << "---";
    std::cout << "  [" << engine.bidDepth() << " bids, "
              << engine.askDepth() << " asks]\n";
}

static void printSeparator()
{
    std::cout << "  ----------------------------------------\n";
}

int main()
{
    std::cout << "\n"
              << "  =============================================\n"
              << "           HFT Order Matching Engine\n"
              << "  =============================================\n\n";

    constexpr size_t ARENA_SIZE = 1'000'000;
    constexpr size_t RING_SIZE = 1024;

    OrderArena arena(ARENA_SIZE);
    MatchingEngine engine;
    SPSCRingBuffer<UDPPacket, RING_SIZE> ring_buffer;

    // ======== Phase 1: Basic Matching ========
    std::cout << "  [Phase 1] Basic Order Matching\n";
    printSeparator();

    Order* a1 = arena.allocateOrder(1, 102, 100, Side::ASK, 1);
    std::cout << "  ADD ASK #1: 100@102\n";
    engine.processOrder(a1);
    printBBO(engine);

    Order* a2 = arena.allocateOrder(2, 103, 100, Side::ASK, 2);
    std::cout << "  ADD ASK #2: 100@103\n";
    engine.processOrder(a2);
    printBBO(engine);

    Order* b1 = arena.allocateOrder(3, 105, 150, Side::BID, 3);
    std::cout << "  ADD BID #3: 150@105 (aggressive, crosses book)\n";
    engine.processOrder(b1);
    printBBO(engine);
    std::cout << "\n";

    // ======== Phase 2: FIFO Priority Demo ========
    engine.reset();
    arena.reset();

    std::cout << "  [Phase 2] FIFO Time Priority Demonstration\n";
    printSeparator();

    Order* fa1 = arena.allocateOrder(10, 100, 50, Side::ASK, 100);
    Order* fa2 = arena.allocateOrder(11, 100, 50, Side::ASK, 200);
    std::cout << "  ADD ASK #10: 50@100 (t=100, Alice)\n";
    engine.processOrder(fa1);
    std::cout << "  ADD ASK #11: 50@100 (t=200, Bob)\n";
    engine.processOrder(fa2);
    printBBO(engine);

    Order* fb = arena.allocateOrder(12, 100, 50, Side::BID, 300);
    std::cout << "  ADD BID #12: 50@100 (Charlie buys)\n";
    engine.processOrder(fb);

    std::cout << "  Result: ASK #10 (Alice, older) filled="
              << (fa1->quantity == 0 ? "YES" : "NO")
              << ", ASK #11 (Bob, newer) filled="
              << (fa2->quantity == 0 ? "YES" : "NO") << "\n";
    printBBO(engine);
    std::cout << "\n";

    // ======== Phase 3: Cancel Order Demo ========
    engine.reset();
    arena.reset();

    std::cout << "  [Phase 3] Cancel Order Demonstration\n";
    printSeparator();

    Order* c1 = arena.allocateOrder(20, 100, 100, Side::ASK, 400);
    Order* c2 = arena.allocateOrder(21, 100, 100, Side::ASK, 500);
    std::cout << "  ADD ASK #20: 100@100\n";
    engine.processOrder(c1);
    std::cout << "  ADD ASK #21: 100@100\n";
    engine.processOrder(c2);
    printBBO(engine);

    std::cout << "  CANCEL ASK #20\n";
    engine.cancelOrder(20);

    Order* c3 = arena.allocateOrder(22, 100, 100, Side::BID, 600);
    std::cout << "  ADD BID #22: 100@100 (should match #21, not cancelled #20)\n";
    engine.processOrder(c3);

    std::cout << "  Result: ASK #20 cancelled, ASK #21 filled="
              << (c2->quantity == 0 ? "YES" : "NO") << "\n";
    printBBO(engine);
    std::cout << "\n";

    // ======== Phase 4: Throughput Stress Test via Ring Buffer ========
    std::cout << "  [Phase 4] Ring Buffer Throughput Stress Test\n";
    printSeparator();

    constexpr size_t STRESS_COUNT = 50000;
    std::atomic<size_t> processed{0};

    // Consumer thread: pops packets, allocates orders, feeds engine
    std::thread consumer([&]()
    {
        UDPPacket pkt;
        MatchingEngine local_engine;
        OrderArena local_arena(STRESS_COUNT);
        size_t count = 0;

        // Suppress matching output for stress test
        std::streambuf* old = std::cout.rdbuf(nullptr);

        while (count < STRESS_COUNT)
        {
            if (ring_buffer.pop(pkt))
            {
                if (pkt.type == OrderType::CANCEL)
                {
                    local_engine.cancelOrder(pkt.id);
                }
                else
                {
                    Order* o = local_arena.allocateOrder(
                        pkt.id, pkt.price, pkt.quantity, pkt.side, pkt.timestamp);
                    if (o) local_engine.processOrder(o);
                }
                count++;
            }
        }

        std::cout.rdbuf(old);
        processed = count;
    });

    // Producer (main thread): generates random orders
    std::mt19937 rng(42);
    std::normal_distribution<double> price_dist(10000.0, 50.0);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 500);
    std::uniform_real_distribution<double> side_dist(0.0, 1.0);

    auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < STRESS_COUNT; i++)
    {
        UDPPacket pkt;
        pkt.id = i + 1;
        pkt.price = static_cast<uint32_t>(std::max(1.0, price_dist(rng)));
        pkt.quantity = qty_dist(rng);
        pkt.timestamp = i;

        double r = side_dist(rng);
        if (r < 0.10 && i > 10)
        {
            pkt.type = OrderType::CANCEL;
            pkt.id = i / 2;
        }
        else
        {
            pkt.type = OrderType::NEW;
            pkt.side = (r < 0.55) ? Side::BID : Side::ASK;
        }

        while (!ring_buffer.push(pkt)) { /* spin */ }
    }

    consumer.join();
    auto end = std::chrono::steady_clock::now();

    double elapsed = std::chrono::duration<double>(end - start).count();
    double throughput = static_cast<double>(STRESS_COUNT) / elapsed;

    std::cout << "  Orders processed: " << processed.load() << "\n"
              << "  Elapsed time:     " << std::fixed << std::setprecision(4)
              << elapsed << " sec\n"
              << "  Throughput:       " << std::setprecision(0) << throughput
              << " orders/sec\n\n";

    // ======== Summary ========
    std::cout << "  =============================================\n"
              << "           All Phases Complete\n"
              << "  =============================================\n\n";

    return 0;
}
