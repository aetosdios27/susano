# Phase 3 Main and Delta Lifecycle

Phase 3 establishes Susano's first storage lifecycle: one immutable dictionary-encoded main column, one mutable raw delta column, and one unified logical row space. It deliberately does not implement merge or concurrent publication.

## Architecture

```text
logical MainDeltaColumn<T>
    ├── immutable MainColumn<T>
    │       └── sorted dictionary + fixed uint32 IDs + ValidityBitmap
    └── append-only DeltaColumn<T>
            └── raw FixedColumn<T>
```

`MainColumn<T>` can be constructed from a raw `FixedColumn<T>`, but exposes no append or mutable dictionary API afterward. `DeltaColumn<T>` owns a contiguous raw `FixedColumn<T>` and supports only value and null appends. `MainDeltaColumn<T>` owns both and routes reads without materializing a combined array.

## Main representation decision

The Phase 3 main uses a sorted dictionary with fixed `std::uint32_t` IDs. Phase 2 found that fixed IDs won most encoded equality and range comparisons, while scalar packed-ID scans and predicates lost consistently despite their lower memory use. The immutable main therefore uses the execution-friendly fixed-ID baseline.

Packed dictionary columns remain available as a Phase 2 experimental representation. They are not deleted and are not claimed to be permanently inferior. This choice is the Phase 3 execution-friendly baseline, subject to later representation tournaments.

## Delta representation decision

The delta uses raw `FixedColumn<T>` storage because foreground append must not pay immutable dictionary construction cost. Phase 2 measured dictionary builds at substantially more cost than raw append/build. The Phase 3 delta therefore prioritizes simple contiguous append and exact null handling over compression.

The delta is temporary mutable state, but Phase 3 does not assume that raw columnar delta is permanently optimal. Row-oriented deltas, historical L1/L2 layouts, immutable mini-segments, or overlays require separate measured experiments.

This is Susano's baseline derived from public main-plus-delta principles and Susano's own Phase 2 measurements. It is not a claim about SAP HANA's proprietary current physical implementation.

## Logical row addressing

For main size `M` and delta size `D`:

```text
main logical RowIds:  [0, M)
delta logical RowIds: [M, M + D)
```

`resolve_main_delta_row` returns a physical domain and a domain-local offset. Delta-local offsets are explicitly `std::size_t` offsets, not logical `RowId` values. Every valid logical row maps to exactly one domain. `MainDeltaColumn::size()` is always `main_size() + delta_size()`.

Point reads route `RowId(M - 1)` to the final main row and `RowId(M)` to delta offset zero. Appending changes only the delta size and never shifts existing logical rows.

## Dictionary-domain invariant

A dictionary ID has meaning only inside the dictionary that owns it. Equal integer IDs from independent dictionaries do not imply equal logical values. Phase 3 never compares a main ID with a delta value or with an ID from another dictionary:

- main equality performs one lookup in the main dictionary and scans main-owned IDs;
- delta equality compares raw values;
- main ranges derive an ordered boundary from the main dictionary and scan main-owned IDs;
- delta ranges compare raw values.

This local-domain rule is required for future merge publication and joins, but Phase 3 does not add a scoped-ID framework.

## Unified predicates

`main_delta_equal`, `main_delta_less`, `main_delta_less_equal`, `main_delta_greater`, and `main_delta_greater_equal` execute one logical operation through two physical paths. Main matches already use logical IDs in `[0, M)`. Delta matches add `M` to each delta-local offset before publication. Results are sorted logical `RowId` vectors and exclude nulls.

The two-domain implementation is intentional: one logical operation may execute differently over different physical representations.

## Invariants and current limits

- main storage is immutable after construction;
- main uses fixed IDs, never packed IDs;
- delta append performs no dictionary construction and cannot mutate main;
- null validity remains separate in both domains;
- logical size equals main size plus delta size;
- main and delta row ranges are disjoint and exhaustive;
- only append and read operations exist;
- mutation is not thread-safe;
- there is no update, delete, or merge operation.

Generated tests build an initial main, perform thousands of deterministic value/null appends, issue point reads and predicates, and compare every sampled result with a `vector<optional<T>>` reference. Tests also snapshot main dictionary values, codes, validity, and size before delta growth and verify that none changes.

## Deferred historical L1/L2 experiment

Phase 3 does not implement the historical row-L1 to column-L2 to main design described in public HANA literature. Main-plus-delta semantics are the current baseline. L1/L2, mini-segments, and overlay designs remain future experiments that must compete through correctness and measurement rather than historical imitation.

## Explicitly deferred to Phase 4 or later

No merge API, closed delta generation, replacement main, background worker, atomic publication, retired-generation reclamation, MVCC, locks, WAL, adaptive compression, SIMD kernel, SQL layer, table abstraction, or parallel execution is present.

## Benchmark methodology

Build and run:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSUSANO_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/susano_bench_main_delta --sweep all > main_delta.csv
```

The required sweep uses one-million-row mains, cardinality 1,000, no nulls, all three physical types, uniform and deterministic skewed distributions, and delta/main ratios:

```text
0, 0.001, 0.01, 0.05, 0.10, 0.25, 0.50, 1.00
```

One main is constructed per type/distribution pair. The delta then grows progressively through the checkpoints; the main is not rebuilt between ratios. Append timing covers only each newly appended delta segment. Main construction times only conversion from the prepared raw source into `MainColumn`.

Each query runs one warmup and one measured pass. Point reads use 100,003 deterministic logical row IDs. Equality and range measurements execute the production two-domain predicates and include result-vector materialization. Sequential scans process main then delta without building a combined decoded array. Checksums or result counts make every operation observable.

For query `Q`, the emitted Susano delta penalty is:

```text
latency(Q at measured ratio) / latency(Q with empty delta)
```

It is a total-latency ratio, not normalized per logical row. The benchmark also emits nanoseconds per processed value. No merge threshold is derived or encoded.

A single operation can be repeated so hardware counters mostly cover the hot path:

```sh
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses \
    ./build-release/susano_bench_main_delta \
    --main-rows 1000000 --cardinality 1000 --delta-permille 100 \
    --distribution uniform --type int64 --operation equality \
    --iterations 500 --seed 42
```

In isolated mode, empty-delta baselines run once for penalty calculation; the selected target operation runs for the requested iterations.

Measurements were recorded on an Intel Core Ultra 9 185H running x86-64 Fedora Linux with kernel `7.1.8-200.fc44.x86_64`, GCC 16.1.1, and CMake Release. CPU affinity and frequency were not controlled. Results are single runs, not universal claims.

## Delta growth results

Representative uniform `int64` configuration:

```text
main rows:       1,000,000
cardinality:     1,000
dictionary:      8,000 bytes
fixed-ID codes:  4,000,000 bytes
main validity:   125,000 bytes
main build:      171.4 ms
```

| D/M | Delta rows | Total B/value | Append ns/value | Mixed point ns/read | Equality latency ms | Equality penalty | Range latency ms | Range penalty | Scan ns/value | Scan penalty |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 4.133 | 0 | 7.391 | 1.576 | 1.00 | 8.926 | 1.00 | 1.012 | 1.00 |
| 0.001 | 1,000 | 4.137 | 8.379 | 3.021 | 1.277 | 0.81 | 8.144 | 0.91 | 1.013 | 1.00 |
| 0.01 | 10,000 | 4.173 | 7.183 | 3.025 | 1.327 | 0.84 | 11.233 | 1.26 | 1.021 | 1.02 |
| 0.05 | 50,000 | 4.323 | 7.127 | 3.329 | 1.353 | 0.86 | 9.983 | 1.12 | 1.094 | 1.14 |
| 0.10 | 100,000 | 4.496 | 7.544 | 5.410 | 1.472 | 0.93 | 9.910 | 1.11 | 1.120 | 1.22 |
| 0.25 | 250,000 | 4.931 | 6.716 | 12.321 | 3.084 | 1.96 | 11.382 | 1.28 | 1.087 | 1.34 |
| 0.50 | 500,000 | 5.464 | 6.700 | 13.094 | 2.251 | 1.43 | 13.427 | 1.50 | 1.083 | 1.61 |
| 1.00 | 1,000,000 | 6.129 | 7.021 | 11.802 | 4.690 | 2.98 | 17.502 | 1.96 | 1.124 | 2.22 |

The raw delta append segments measured 6.7–8.4 ns/value in this configuration. Immutable main construction measured about 171 ns per main row. These timings cover different lifecycle operations, but they demonstrate why rebuilding the dictionary on each append is not viable.

Memory rose from 4.133 bytes per logical value with an empty delta to 6.129 bytes at `D/M = 1.0`. The raw delta therefore eroded, but did not completely remove, the fixed-ID main's size advantage over an 8.125-byte raw `int64` column.

Single-pass latency is noisy at the smallest ratios: several penalties below 1.0 are measurement variation, not a speedup claim. In this run, sustained degradation was visible by `D/M = 0.25` for equality, mixed point reads, and sequential total latency. At `D/M = 1.0`, equality, range, and sequential penalties were 2.98, 1.96, and 2.22 respectively. The sweep produces a curve; it does not justify a hard-coded merge threshold.

Uniform and skewed sweeps for `int32`, `int64`, and `float64` completed with matching checksums and result counts. Distribution changed absolute timings, but the same architectural effect remained: append stayed cheap while total query work and raw-delta memory increased with delta size.

## Isolated performance counters

The representative 500-iteration equality command above measured 0.721 seconds inside the target operation and 0.950 seconds process elapsed, so repeated equality dominated the process more clearly than in Phase 2's process-wide benchmark. Performance-core counters were:

| Event | Count |
|---|---:|
| cycles | 1,824,797,937 |
| instructions | 6,149,792,436 |
| cache references | 41,093,305 |
| cache misses | 903,450 |
| branches | 1,752,964,106 |
| branch misses | 11,743,380 |

The counters still include setup, one-pass baselines, append, validation, and output. They are representative of an operation-dominated process, not a perfectly isolated kernel or per-domain attribution.

## Phase 4 implications

- Readers must continue seeing one stable main and one stable logical delta generation while a replacement main is built.
- Main construction needs an immutable input generation because it costs orders of magnitude more per row than foreground delta append.
- Future merge publication must preserve logical row order and nullness while dictionary IDs are reassigned into a new dictionary domain.
- Temporary merge memory must account for the old main, closed delta, new dictionary, new codes, and active write delta simultaneously.
- Equality and total scan latency showed clearer growth than normalized scan cost; Phase 4 should measure publication mechanics without inventing a threshold from this single curve.
- Point-read measurements were cache-sensitive and non-monotonic, so future merge-policy work needs repetitions and controlled affinity before using them.
- A new active delta, atomic publication, and retired-generation lifetime are Phase 4 problems; none is pre-scaffolded here.
