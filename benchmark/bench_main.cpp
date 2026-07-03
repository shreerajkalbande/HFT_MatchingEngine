#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "../BitmapOrderBook.h"
#include "../MatchingEngine.h"
#include "../OrderArena.h"
#include "../PriceLevel.h"

namespace
{
constexpr size_t REPEATS = 7;
volatile uint64_t benchmark_sink = 0;

struct Stats
{
    double min_ops_per_sec;
    double median_ops_per_sec;
    double max_ops_per_sec;
};

Stats summarize(std::vector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    return {samples.front(), samples[samples.size() / 2], samples.back()};
}

template <typename Engine>
Stats benchPassiveInsert(size_t levels_per_side)
{
    constexpr size_t ORDER_COUNT = 100'000;
    std::vector<double> samples;
    samples.reserve(REPEATS);

    for (size_t repeat = 0; repeat < REPEATS; ++repeat)
    {
        OrderArena arena(ORDER_COUNT);
        Engine engine(arena, ORDER_COUNT);

        const auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < ORDER_COUNT; ++i)
        {
            const bool is_bid = (i & 1U) == 0;
            const uint32_t level =
                static_cast<uint32_t>((i / 2) % levels_per_side);
            const uint32_t price =
                is_bid ? 9'999U - level : 10'001U + level;
            Order* order = arena.allocateOrder(
                i + 1, price, 100, is_bid ? Side::BID : Side::ASK, i);
            if (!engine.processOrder(order))
                throw std::runtime_error("passive insert rejected an order");
        }
        const auto end = std::chrono::steady_clock::now();

        if (engine.totalOrders() != ORDER_COUNT ||
            engine.bidLevels() != levels_per_side ||
            engine.askLevels() != levels_per_side)
        {
            throw std::runtime_error("passive insert produced an invalid book");
        }
        benchmark_sink += engine.getBBO().best_bid_qty + repeat;
        const double seconds = std::chrono::duration<double>(end - start).count();
        samples.push_back(static_cast<double>(ORDER_COUNT) / seconds);
    }

    return summarize(std::move(samples));
}

template <typename Engine>
Stats benchCancellation()
{
    constexpr size_t ORDER_COUNT = 100'000;
    constexpr size_t LEVELS_PER_SIDE = 100;
    constexpr size_t PERMUTATION_STEP = 7'919;
    std::vector<double> samples;
    samples.reserve(REPEATS);

    for (size_t repeat = 0; repeat < REPEATS; ++repeat)
    {
        OrderArena arena(ORDER_COUNT);
        Engine engine(arena, ORDER_COUNT);

        for (size_t i = 0; i < ORDER_COUNT; ++i)
        {
            const bool is_bid = (i & 1U) == 0;
            const uint32_t level =
                static_cast<uint32_t>((i / 2) % LEVELS_PER_SIDE);
            const uint32_t price =
                is_bid ? 9'999U - level : 10'001U + level;
            engine.processOrder(arena.allocateOrder(
                i + 1, price, 100, is_bid ? Side::BID : Side::ASK, i));
        }

        const auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < ORDER_COUNT; ++i)
        {
            const uint64_t id = ((i * PERMUTATION_STEP) % ORDER_COUNT) + 1;
            if (!engine.cancelOrder(id))
                throw std::runtime_error("cancellation generated an invalid ID");
        }
        const auto end = std::chrono::steady_clock::now();

        if (engine.totalOrders() != 0 || engine.bidLevels() != 0 ||
            engine.askLevels() != 0)
        {
            throw std::runtime_error("cancellation left orders in the book");
        }

        benchmark_sink += repeat;
        const double seconds = std::chrono::duration<double>(end - start).count();
        samples.push_back(static_cast<double>(ORDER_COUNT) / seconds);
    }

    return summarize(std::move(samples));
}

template <typename Engine>
Stats benchBestLevelDepletion()
{
    constexpr size_t LEVEL_COUNT = 10'000;
    std::vector<double> samples;
    samples.reserve(REPEATS);

    for (size_t repeat = 0; repeat < REPEATS; ++repeat)
    {
        OrderArena arena(LEVEL_COUNT * 2);
        Engine engine(arena, LEVEL_COUNT);

        for (size_t i = 0; i < LEVEL_COUNT; ++i)
        {
            engine.processOrder(arena.allocateOrder(
                i + 1, 10'001U + static_cast<uint32_t>(i),
                1, Side::ASK, i));
        }

        const auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < LEVEL_COUNT; ++i)
        {
            const uint64_t id = LEVEL_COUNT + i + 1;
            engine.processOrder(arena.allocateOrder(
                id, BitmapOrderBook::MAX_PRICE, 1, Side::BID, id));
        }
        const auto end = std::chrono::steady_clock::now();

        if (engine.totalOrders() != 0)
            throw std::runtime_error("best-level depletion left orders");

        benchmark_sink += repeat;
        const double seconds = std::chrono::duration<double>(end - start).count();
        samples.push_back(static_cast<double>(LEVEL_COUNT) / seconds);
    }

    return summarize(std::move(samples));
}

void printComparison(const std::string& workload,
                     size_t operations,
                     const Stats& map,
                     const Stats& bitmap)
{
    const double speedup = bitmap.median_ops_per_sec / map.median_ops_per_sec;
    std::cout << "\n" << workload << " (" << operations << " operations/trial)\n"
              << "  map:     " << std::fixed << std::setprecision(0)
              << std::setw(12) << map.median_ops_per_sec << " ops/s"
              << "  [" << map.min_ops_per_sec << ", " << map.max_ops_per_sec
              << "]\n"
              << "  bitmap:  " << std::setw(12) << bitmap.median_ops_per_sec
              << " ops/s"
              << "  [" << bitmap.min_ops_per_sec << ", "
              << bitmap.max_ops_per_sec << "]\n"
              << "  ratio:   " << std::setprecision(2) << speedup
              << "x bitmap/map\n";
}
} // namespace

int main()
{
    std::cout << "\nMap vs Hierarchical-Bitmap Price Index\n"
              << "Same matching logic, order arena, ID hash, FIFO, and event stream.\n"
              << "Median and range across " << REPEATS << " trials.\n";

    printComparison("Passive insert: concentrated (100 levels/side)", 100'000,
                    benchPassiveInsert<MapMatchingEngine>(100),
                    benchPassiveInsert<BitmapMatchingEngine>(100));

    printComparison("Passive insert: dispersed (5,000 levels/side)", 100'000,
                    benchPassiveInsert<MapMatchingEngine>(5'000),
                    benchPassiveInsert<BitmapMatchingEngine>(5'000));

    printComparison("Direct cancellation (100 levels/side)", 100'000,
                    benchCancellation<MapMatchingEngine>(),
                    benchCancellation<BitmapMatchingEngine>());

    printComparison("Best-level depletion (10,000 levels)", 10'000,
                    benchBestLevelDepletion<MapMatchingEngine>(),
                    benchBestLevelDepletion<BitmapMatchingEngine>());

    BitmapOrderBook bitmap_book;
    std::cout << "\nBitmap backend fixed storage per book\n"
              << "  dense price levels: " << bitmap_book.denseStorageBytes()
              << " bytes\n"
              << "  hierarchy bitmaps:  " << bitmap_book.bitmapStorageBytes()
              << " bytes\n"
              << "  PriceLevel size:     " << sizeof(PriceLevel) << " bytes\n"
              << "  price domain:        [" << BitmapOrderBook::MIN_PRICE
              << ", " << BitmapOrderBook::MAX_PRICE << "]\n"
              << "\nValidation checksum: " << benchmark_sink << "\n\n";
    return 0;
}
