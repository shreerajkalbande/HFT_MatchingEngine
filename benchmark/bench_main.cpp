#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>

#include "../OrderArena.h"
#include "../MatchingEngine.h"
#include "../SPSCRingBuffer.h"

// ========================== Statistics ==========================

struct LatencyStats
{
    int64_t min_ns;
    int64_t max_ns;
    double mean_ns;
    int64_t p50_ns;
    int64_t p95_ns;
    int64_t p99_ns;
    double throughput;  // orders per second
};

static LatencyStats computeStats(std::vector<int64_t>& latencies, double total_seconds)
{
    std::sort(latencies.begin(), latencies.end());
    size_t n = latencies.size();

    LatencyStats stats{};
    stats.min_ns = latencies.front();
    stats.max_ns = latencies.back();
    stats.mean_ns = std::accumulate(latencies.begin(), latencies.end(), 0.0) / static_cast<double>(n);
    stats.p50_ns = latencies[n * 50 / 100];
    stats.p95_ns = latencies[n * 95 / 100];
    stats.p99_ns = latencies[n * 99 / 100];
    stats.throughput = static_cast<double>(n) / total_seconds;
    return stats;
}

static void printStats(const char* label, const LatencyStats& s, size_t count)
{
    std::cout << "\n=== " << label << " ===\n"
              << "  Orders:      " << count << "\n"
              << "  Throughput:  " << std::fixed << std::setprecision(0)
              << s.throughput << " orders/sec\n\n"
              << "  Latency (nanoseconds):\n"
              << "    Min:    " << std::setw(8) << s.min_ns << " ns\n"
              << "    Mean:   " << std::setw(8) << static_cast<int64_t>(s.mean_ns) << " ns\n"
              << "    P50:    " << std::setw(8) << s.p50_ns << " ns\n"
              << "    P95:    " << std::setw(8) << s.p95_ns << " ns\n"
              << "    P99:    " << std::setw(8) << s.p99_ns << " ns\n"
              << "    Max:    " << std::setw(8) << s.max_ns << " ns\n";
}

// ========================== Matching Engine Benchmark ==========================

static void benchMatchingEngine()
{
    constexpr size_t WARMUP_COUNT = 1000;
    constexpr size_t ORDER_COUNT  = 100000;
    constexpr size_t TOTAL = WARMUP_COUNT + ORDER_COUNT;

    OrderArena arena(TOTAL);
    MatchingEngine engine;

    std::mt19937 rng(42);  // fixed seed for reproducibility
    std::normal_distribution<double> price_dist(10000.0, 50.0);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 1000);
    std::uniform_real_distribution<double> type_dist(0.0, 1.0);

    // Pre-generate all orders for cache warmth
    struct OrderInput
    {
        uint32_t price;
        uint32_t qty;
        Side side;
        bool is_cancel;
    };

    std::vector<OrderInput> inputs(TOTAL);
    for (size_t i = 0; i < TOTAL; i++)
    {
        double r = type_dist(rng);
        inputs[i].price = static_cast<uint32_t>(std::max(1.0, price_dist(rng)));
        inputs[i].qty   = qty_dist(rng);
        inputs[i].is_cancel = (r > 0.90);
        inputs[i].side = (r < 0.45) ? Side::BID : Side::ASK;
    }

    // Suppress cout during warmup + benchmark (execution prints are noisy)
    std::streambuf* old_cout = std::cout.rdbuf(nullptr);

    // Warmup phase - not measured
    uint64_t next_id = 1;
    for (size_t i = 0; i < WARMUP_COUNT; i++)
    {
        if (inputs[i].is_cancel && next_id > 2)
        {
            engine.cancelOrder(next_id / 2);
        }
        else
        {
            Order* o = arena.allocateOrder(next_id++, inputs[i].price, inputs[i].qty,
                                           inputs[i].side, i);
            engine.processOrder(o);
        }
    }

    // Measurement phase
    std::vector<int64_t> latencies;
    latencies.reserve(ORDER_COUNT);

    auto total_start = std::chrono::steady_clock::now();

    for (size_t i = WARMUP_COUNT; i < TOTAL; i++)
    {
        auto start = std::chrono::steady_clock::now();

        if (inputs[i].is_cancel && next_id > 2)
        {
            engine.cancelOrder(next_id / 2);
        }
        else
        {
            Order* o = arena.allocateOrder(next_id++, inputs[i].price, inputs[i].qty,
                                           inputs[i].side, i);
            engine.processOrder(o);
        }

        auto end = std::chrono::steady_clock::now();
        latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    auto total_end = std::chrono::steady_clock::now();

    // Restore cout
    std::cout.rdbuf(old_cout);

    double total_sec = std::chrono::duration<double>(total_end - total_start).count();
    auto stats = computeStats(latencies, total_sec);
    printStats("Matching Engine Benchmark", stats, ORDER_COUNT);
}

// ========================== Ring Buffer Benchmark ==========================

static void benchRingBuffer()
{
    constexpr size_t MSG_COUNT = 1000000;
    SPSCRingBuffer<uint64_t, 4096> rb;

    std::atomic<bool> consumer_done{false};
    uint64_t sum_consumer = 0;

    auto total_start = std::chrono::steady_clock::now();

    // Consumer thread
    std::thread consumer([&]()
    {
        uint64_t val;
        size_t consumed = 0;
        while (consumed < MSG_COUNT)
        {
            if (rb.pop(val))
            {
                sum_consumer += val;
                consumed++;
            }
        }
        consumer_done = true;
    });

    // Producer (main thread)
    for (uint64_t i = 0; i < MSG_COUNT; i++)
    {
        while (!rb.push(i))
        {
            // spin until slot available
        }
    }

    consumer.join();
    auto total_end = std::chrono::steady_clock::now();

    double total_sec = std::chrono::duration<double>(total_end - total_start).count();
    double throughput = static_cast<double>(MSG_COUNT) / total_sec;

    std::cout << "\n=== SPSC Ring Buffer Benchmark ===\n"
              << "  Messages:    " << MSG_COUNT << "\n"
              << "  Total time:  " << std::fixed << std::setprecision(4) << total_sec << " sec\n"
              << "  Throughput:  " << std::setprecision(0) << throughput << " msgs/sec\n"
              << "  Avg latency: " << std::setprecision(1)
              << (total_sec / static_cast<double>(MSG_COUNT)) * 1e9 << " ns/msg\n";
}

// ========================== std::map vs std::vector Comparison ==========================

#include <map>

// std::map-based order book: each Order* is a separate node in the red-black tree.
// Tree nodes are heap-allocated and scattered across memory → pointer chasing,
// cache misses on every traversal, and O(log n) tree rebalancing on every remove.
class MapOrderBook
{
    // multimap: key=price, each order is its own RB-tree node
    // bids: highest price = best → use reverse iterator
    // asks: lowest price = best → use forward iterator
    std::multimap<uint32_t, Order*> bids;
    std::multimap<uint32_t, Order*, std::greater<uint32_t>> asks;  // descending

public:
    void processOrder(Order* incoming)
    {
        if (incoming->side == Side::BID)
        {
            // Match against asks (lowest first = begin())
            while (incoming->quantity > 0 && !asks.empty())
            {
                auto it = asks.begin();  // best ask (lowest price)
                if (incoming->price < it->first) break;

                Order* resting = it->second;
                uint32_t traded = std::min(incoming->quantity, resting->quantity);
                incoming->quantity -= traded;
                resting->quantity -= traded;
                if (resting->quantity == 0)
                {
                    resting->is_active = false;
                    asks.erase(it);  // RB-tree delete + rebalance
                }
            }
            if (incoming->quantity > 0)
                bids.insert({incoming->price, incoming});  // RB-tree insert
            else
                incoming->is_active = false;
        }
        else
        {
            // Match against bids (highest first = begin() since bids is default ascending, use rbegin)
            while (incoming->quantity > 0 && !bids.empty())
            {
                auto it = std::prev(bids.end());  // best bid (highest price)
                if (incoming->price > it->first) break;

                Order* resting = it->second;
                uint32_t traded = std::min(incoming->quantity, resting->quantity);
                incoming->quantity -= traded;
                resting->quantity -= traded;
                if (resting->quantity == 0)
                {
                    resting->is_active = false;
                    bids.erase(it);  // RB-tree delete + rebalance
                }
            }
            if (incoming->quantity > 0)
                asks.insert({incoming->price, incoming});  // RB-tree insert
            else
                incoming->is_active = false;
        }
    }
};

static void benchMapComparison()
{
    constexpr size_t ORDER_COUNT = 100000;

    std::mt19937 rng(42);
    std::normal_distribution<double> price_dist(10000.0, 50.0);
    std::uniform_int_distribution<uint32_t> qty_dist(1, 1000);
    std::uniform_real_distribution<double> side_dist(0.0, 1.0);

    struct OrderInput { uint32_t price; uint32_t qty; Side side; };
    std::vector<OrderInput> inputs(ORDER_COUNT);
    for (size_t i = 0; i < ORDER_COUNT; i++)
    {
        inputs[i].price = static_cast<uint32_t>(std::max(1.0, price_dist(rng)));
        inputs[i].qty   = qty_dist(rng);
        inputs[i].side  = (side_dist(rng) < 0.5) ? Side::BID : Side::ASK;
    }

    // Suppress execution output
    std::streambuf* old_cout = std::cout.rdbuf(nullptr);

    // ----- Benchmark: Arena + std::vector (ours) -----
    double throughput_vec;
    {
        OrderArena arena(ORDER_COUNT);
        MatchingEngine engine;

        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < ORDER_COUNT; i++)
        {
            Order* o = arena.allocateOrder(i + 1, inputs[i].price, inputs[i].qty,
                                           inputs[i].side, i);
            engine.processOrder(o);
        }
        auto end = std::chrono::steady_clock::now();
        throughput_vec = static_cast<double>(ORDER_COUNT) / std::chrono::duration<double>(end - start).count();
    }

    // ----- Benchmark: Heap alloc + std::map (baseline) -----
    // Each order is new'd individually (scattered heap) and stored
    // as a separate RB-tree node in std::multimap<price, Order*>
    double throughput_map;
    {
        MapOrderBook map_book;
        std::vector<Order*> heap_orders;
        heap_orders.reserve(ORDER_COUNT);

        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < ORDER_COUNT; i++)
        {
            Order* o = new Order();
            o->id = i + 1;
            o->price = inputs[i].price;
            o->quantity = inputs[i].qty;
            o->side = inputs[i].side;
            o->timestamp = i;
            o->is_active = true;
            heap_orders.push_back(o);
            map_book.processOrder(o);
        }
        auto end = std::chrono::steady_clock::now();
        throughput_map = static_cast<double>(ORDER_COUNT) / std::chrono::duration<double>(end - start).count();

        for (Order* o : heap_orders) delete o;
    }

    // Restore cout
    std::cout.rdbuf(old_cout);

    std::cout << "\n=== std::vector+Arena vs std::map+Heap (" << ORDER_COUNT << " orders) ===\n"
              << "  vector + arena (ours):  " << std::fixed << std::setprecision(0)
              << throughput_vec << " orders/sec\n"
              << "  map + heap (baseline):  "
              << throughput_map << " orders/sec\n"
              << "  Speedup:                " << std::setprecision(1)
              << throughput_vec / throughput_map << "x\n";
}

// ========================== Main ==========================

int main()
{
    std::cout << "\n======================================\n"
              << "  HFT Matching Engine Benchmark Suite\n"
              << "======================================\n";

    benchMatchingEngine();
    benchRingBuffer();
    benchMapComparison();

    std::cout << "\n======================================\n"
              << "  Benchmark Complete\n"
              << "======================================\n\n";

    return 0;
}
