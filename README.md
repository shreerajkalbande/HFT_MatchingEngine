# Map vs Hierarchical-Bitmap Matching Engine

A C++17 price-time-priority matching engine built to compare two price-index
data structures under identical matching semantics:

- `MapOrderBook`: sparse `std::map` price levels
- `BitmapOrderBook`: dense price levels plus a hierarchical occupancy bitmap

The project is deliberately about data structures. Networking, persistence,
threading, and exchange protocol handling are outside its scope.

## Architecture

```text
                         BasicMatchingEngine<Book>
                                   |
                 +-----------------+-----------------+
                 |                                   |
          MapOrderBook                       BitmapOrderBook
                 |                                   |
     std::map<price, PriceLevel>       dense PriceLevel[price range]
                                                     +
                                          hierarchical bitmap
                 |                                   |
                 +-----------------+-----------------+
                                   |
                    PriceLevel intrusive FIFO
                    head <-> order <-> tail
                                   |
                         OrderArena owns Orders

          unordered_map<order_id, Order*> is used by both backends
                         for direct cancellation
```

The matching algorithm is compiled against the selected backend:

```cpp
MapMatchingEngine map_engine;
BitmapMatchingEngine bitmap_engine;
```

There is no virtual dispatch in the matching path.

## Shared matching semantics

Both backends implement:

- Highest bid / lowest ask price priority
- FIFO time priority within one price level
- Partial fills and multi-level sweeps
- Aggregate quantity at each price
- Exact cancellation by order ID
- Immediate removal of empty levels
- Duplicate active-ID rejection
- Optional execution callback

Orders at one price are stored in an intrusive doubly linked FIFO. Appending,
removing the head, and unlinking a cancelled order are `O(1)` and require no
separate list-node allocation.

## Map backend

The reference backend stores one tree node per active price level:

```text
bids: std::map<price, PriceLevel, greater>
asks: std::map<price, PriceLevel>
```

If `L` is the number of active levels:

- Price lookup, creation, and deletion: `O(log2 L)`
- Best bid/ask: `O(1)` through `begin()`
- Memory is proportional to active levels
- Tree traversal performs dependent pointer reads
- New levels may allocate and tree updates may rebalance

This backend supports sparse and changing price domains naturally.

## Bitmap backend

The optimized backend supports integer prices from `1` through `20,000`.
Prices directly index dense `PriceLevel` arrays.

The first bitmap has one bit per possible price. Each higher-level bit records
whether one 64-bit word below it is non-zero:

```text
20,000 price bits -> 313 words -> 5 words -> 1 word
```

Best-price discovery follows one set bit at each layer using bit-scan
instructions. It does not scan every empty price:

```text
top summary bit
      |
selected middle word
      |
selected price word
      |
PriceLevel[index]
```

For a bounded domain of `P` ticks:

- Known-price access: `O(1)`
- Best-level discovery: `O(log64 P)`
- Level occupancy update: at most one bit update per hierarchy layer
- Memory and traversal are contiguous and allocation-free
- Dense level storage costs memory even when most prices are empty

The bitmap changes only when a level transitions between empty and non-empty.
Orders added to or removed from a still-occupied level do not propagate
summary updates.

## Complexity comparison

| Operation | Map backend | Bitmap backend |
|:--|:--|:--|
| Find known price | `O(log2 L)` | `O(1)` |
| Create/delete level | `O(log2 L)` | `O(log64 P)` occupancy propagation |
| Find best price | `O(1)` cached tree boundary | `O(log64 P)` bit hierarchy |
| Append at level | `O(1)` after lookup | `O(1)` after direct indexing |
| Fill FIFO head | `O(1)` | `O(1)` |
| Cancel by ID | average `O(1)` hash + `O(log2 L)` | average `O(1)` hash + direct index |
| Price-index memory | proportional to active levels | proportional to full price domain |

`L` is active price levels; `P` is representable price ticks. These complexity
expressions do not establish a universal winner. Allocation, cache misses,
branch behavior, active-level density, and event mix determine the crossover.

## Build and run

```bash
make
./hft_engine

make test
make bench
make debug
```

The tests execute the same conformance cases against both backends and compare
their BBO state over an identical deterministic event stream. Bitmap-specific
tests cross 64-bit word and higher-summary boundaries.

## Comparative benchmarks

`make bench` reports median throughput and range across seven trials for:

1. Passive insertion concentrated in 100 levels per side
2. Passive insertion dispersed across 5,000 levels per side
3. Direct cancellation from 100 levels per side
4. Repeated depletion of 10,000 consecutive best levels

Both implementations use the same:

- `BasicMatchingEngine` algorithm
- generated event pattern
- `OrderArena` allocation and O(1) free-list recycling
- `unordered_map<order_id, Order*>`
- intrusive FIFO representation
- compiler flags and validation checks

Backend construction and workload preload are outside timed regions where
applicable. Results are local microbenchmarks, not production exchange latency.
See [BENCHMARK_ANALYSIS.md](BENCHMARK_ANALYSIS.md) for interpretation rules.

## Project limits

- One single-threaded order book
- Integer prices and quantities
- Bitmap prices are bounded to `[1, 20,000]`
- Limit orders only
- No replace, market, stop, iceberg, or auction orders
- No risk checks, journaling, recovery, networking, or concurrency
- Standard `unordered_map` allocations are not pooled
- `OrderArena` reuses slots for cancelled, fully filled, or reset orders

## Summary

> Implemented interchangeable sparse-tree and dense hierarchical-bitmap price
> indexes for a price-time-priority matching engine, while holding matching,
> FIFO, cancellation, and order storage constant. Benchmarked how price-level
> density, level churn, cache locality, fixed-domain memory, and pointer
> traversal affect the performance crossover.

The intended conclusion is not “bitmaps always beat maps.” The bitmap trades a
bounded dense price domain and fixed memory for direct indexing and
cache-friendly fixed-depth discovery. The map trades pointer traversal and
node allocation for sparse-domain flexibility.
