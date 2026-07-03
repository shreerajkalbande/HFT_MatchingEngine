# Benchmark Methodology

## Question being measured

The benchmark asks a narrow question:

> With matching semantics, order storage, ID lookup, FIFO representation, and
> event streams held constant, how does a sparse tree price index compare with
> a bounded dense hierarchical-bitmap price index?

It does not measure network latency, multi-threading, persistence, exchange
protocol parsing, or complete trading-system latency.

## Controlled components

Both benchmark variants use:

- `BasicMatchingEngine<Book>`
- the same generated order IDs, prices, quantities, and sides
- `OrderArena` for stable active-order addresses and O(1) slot recycling
- `unordered_map<uint64_t, Order*>` for cancellation lookup
- intrusive FIFO links in `Order`
- identical correctness validation after every trial
- the same optimized binary and compiler flags

Backend selection is a template parameter. No virtual dispatch is introduced.

## Price-index variants

### Sparse map

One `std::map` node is stored per active price level. It scales with active
levels and supports arbitrary `uint32_t` prices. Its costs include dependent
pointer traversal, node allocation/deallocation, and red-black-tree updates.

### Dense hierarchical bitmap

Two dense `PriceLevel` arrays cover prices `[1, 20,000]`. Separate bid and ask
hierarchies encode occupancy:

```text
20,000 bits -> 313 uint64_t words
313 bits    ->   5 uint64_t words
5 bits      ->   1 uint64_t word
```

A hierarchy lookup selects one word per layer and uses a lowest- or
highest-set-bit operation. Empty/non-empty transitions update at most one bit
per layer.

The benchmark prints exact dense-level and bitmap storage. It does not claim an
exact `std::map` byte count because node size and allocator overhead are
implementation-dependent.

## Workloads

### Concentrated passive insertion

- 100,000 non-crossing orders
- 100 active prices per side
- Many orders share each level

This favors reuse of existing levels and measures lookup plus FIFO append and
ID-hash insertion.

### Dispersed passive insertion

- 100,000 non-crossing orders
- 5,000 active prices per side

This increases map depth and level creation while remaining inside the bitmap
domain.

### Direct cancellation

- 100,000 preloaded orders
- 100 prices per side
- IDs cancelled in a deterministic permutation
- preload excluded from timing

This measures hash lookup, exact FIFO unlinking, level lookup, and occasional
empty-level removal.

### Best-level depletion

- 10,000 asks preloaded at consecutive prices
- 10,000 aggressive bids remove exactly one level each
- preload excluded from timing

This deliberately forces best-price repair after every match. The map erases
its first node; the bitmap clears occupancy and follows its hierarchy to the
next set bit.

## Reporting

- Seven independent trials per backend and workload
- Median throughput plus minimum/maximum range
- Bitmap/map median ratio
- Final state validation and checksum

Throughput is reported instead of claiming per-order end-to-end latency.

## Sample local result

One local run of this benchmark produced the following median throughputs:

| Workload | `std::map` median | Bitmap median | Bitmap/map ratio |
| --- | ---: | ---: | ---: |
| Passive insert, 100 levels/side | 19.17M ops/s | 28.49M ops/s | 1.49x |
| Passive insert, 5,000 levels/side | 14.44M ops/s | 27.99M ops/s | 1.94x |
| Direct cancellation, 100 levels/side | 5.55M ops/s | 5.80M ops/s | 1.04x |
| Best-level depletion, 10,000 levels | 9.09M ops/s | 20.53M ops/s | 2.26x |

The headline resume number comes from the best-level depletion workload: the
hierarchical-bitmap backend reached about 20.5M ops/s and a 2.26x median
speedup over the `std::map` backend in that local run. Across all workloads in
the same run, the highest bitmap throughput observed was about 28.5M ops/s.

These are local microbenchmark results. They should be treated as reproducible
project evidence for this benchmark harness, not as production exchange latency
or a universal claim that bitmaps always outperform maps.

## Interpretation

Expected tendencies, not guaranteed results:

- Concentrated levels reduce map insertion/deletion frequency.
- Dispersed levels increase map traversal and allocation pressure.
- Bitmap cancellation benefits from direct price indexing.
- Best-level depletion stresses bitmap hierarchy maintenance and map erasure.
- A very sparse active book may make the map's lower fixed memory more
  attractive even when the bitmap is faster.

Hardware prefetching, cache sizes, allocator behavior, compiler version,
frequency scaling, and system load can change results.

## Claims this benchmark does not justify

- That either backend is universally faster
- That throughput equals exchange latency
- That a local result transfers to another CPU or allocator
- That the implementation is production-ready
- That the bitmap is appropriate for an unbounded or extremely sparse domain
