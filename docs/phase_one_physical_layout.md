# Phase 1 Physical Column Layout

Phase 1 establishes a raw fixed-width baseline. It is not Susano's final column format.

## Physical types

`PhysicalType` has three explicitly numbered `std::uint8_t` identifiers:

| Identifier | Name | Stored width |
|---|---|---:|
| `Int32` | `int32` | 4 bytes |
| `Int64` | `int64` | 8 bytes |
| `Float64` | `float64` | 8 bytes |

These identifiers describe physical representation only. They are not a SQL logical type system.

## Row identifiers

`RowId` contains one `std::uint64_t`. Construction from an integer is explicit. The type has value semantics and total ordering, with no reserved or sentinel values.

## Validity bitmap

`ValidityBitmap` stores logical validity in a contiguous `std::vector<std::uint64_t>`. Row `r` uses word `r / 64` and bit `r % 64`; a set bit means valid and a clear bit means null. The number of words is exactly `ceil(row_count / 64)`, so the payload cost is one bit per row rounded up to a complete 64-bit word, excluding vector object and capacity overhead.

Appending the first row in a word initializes the entire word to zero before setting any valid bit. Unused high bits in the final word therefore remain zero.

## Fixed-width columns

`FixedColumn<T>` is limited to `std::int32_t`, `std::int64_t`, and `double`. It owns two parallel arrays:

1. a contiguous `std::vector<T>` containing one physical value per row;
2. a `ValidityBitmap` containing one validity bit per row.

A null append writes the deterministic placeholder `T{}` into the value array and appends a clear validity bit. A non-null append writes the supplied value and appends a set validity bit. If validity growth throws after the value append, the value append is rolled back.

The read-only `values()` span exposes the contiguous physical array for direct scans. Scanning it performs no allocation, virtual dispatch, variant visitation, or per-value pointer chasing.

## Bounds policy and invariants

`value`, `is_null`, and bitmap validity reads require `row.value() < size()`. Debug builds assert this precondition. Release builds treat it as a caller precondition and use unchecked indexed access to avoid an additional hot-path branch.

The implementation and tests enforce these invariants:

- `values().size() == validity().size() == column.size()` after every successful append;
- every logical row has exactly one initialized `T` value and one validity bit;
- null rows contain `T{}` and have a clear validity bit;
- non-null values round-trip exactly, including the stored bit pattern for tested `double` values;
- bitmap word count is `ceil(size / 64)` and accesses cross word boundaries correctly;
- a failed validity allocation cannot leave the value and validity lengths divergent.

Mutation is append-only and not thread-safe.

## Sequential scan benchmark

Build and run the release benchmark with:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSUSANO_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/susano_bench_fixed_column
```

Pass one positive row count to run only that size, which makes the executable convenient for `perf`:

```sh
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses \
    ./build-release/susano_bench_fixed_column 10000000
```

Column construction is outside the internal timer. Each case performs two explicit warmup scans, then enough direct scans of `FixedColumn<T>::values()` to visit approximately 50 million values. The checksum is emitted with the measurements so the scan remains observable. CSV output records type, logical rows, scan iterations, elapsed nanoseconds, rows per second, bytes per second, nanoseconds per value, and checksum.

### Baseline recorded 2026-08-20

Environment: Intel Core Ultra 9 185H, x86-64 Linux, GCC 16.1.1, CMake `Release`. CPU placement and frequency were not pinned. These are single-run reference measurements, not performance claims.

| Type | Rows | Iterations | Rows/s | Bytes/s | ns/value |
|---|---:|---:|---:|---:|---:|
| int32 | 1,024 | 48,828 | 3.83614e9 | 1.53446e10 | 0.260679 |
| int64 | 1,024 | 48,828 | 3.59525e9 | 2.87620e10 | 0.278144 |
| float64 | 1,024 | 48,828 | 9.13769e8 | 7.31015e9 | 1.09437 |
| int32 | 100,000 | 500 | 4.25809e9 | 1.70324e10 | 0.234847 |
| int64 | 100,000 | 500 | 3.64612e9 | 2.91690e10 | 0.274264 |
| float64 | 100,000 | 500 | 9.60421e8 | 7.68337e9 | 1.04121 |
| int32 | 1,000,000 | 50 | 4.42454e9 | 1.76982e10 | 0.226012 |
| int64 | 1,000,000 | 50 | 2.35153e9 | 1.88123e10 | 0.425255 |
| float64 | 1,000,000 | 50 | 9.59086e8 | 7.67268e9 | 1.04266 |
| int32 | 10,000,000 | 5 | 2.71044e9 | 1.08418e10 | 0.368943 |
| int64 | 10,000,000 | 5 | 1.58185e9 | 1.26548e10 | 0.632170 |
| float64 | 10,000,000 | 5 | 9.50713e8 | 7.60570e9 | 1.05184 |

A representative `perf stat` invocation used 10,000,000 rows for all three types. Linux reported hybrid-core counters separately:

| Event | Performance cores (98.53% coverage) | Efficiency cores (1.47% coverage) |
|---|---:|---:|
| cycles | 643,862,352 | 313,381,030 |
| instructions | 1,276,384,473 | 776,528,191 |
| cache references | 27,806,054 | 3,431,894 |
| cache misses | 9,039,853 | 1,271,845 |
| branches | 181,473,109 | 94,671,489 |
| branch misses | 472,002 | 29,055 |

The `perf` process-level counters include column construction, warmup, output, and all three typed cases; the benchmark's elapsed fields include only measured scans. Preserve that distinction in comparisons.

## Current limitations

There are no variable-width values, encodings, compression, tables, logical types, execution operators, persistence, transactions, or concurrent mutation semantics. The bitmap has no SIMD operations or rank/select index. The column has no update or delete operation.

Future physical representations must justify added complexity through a correctness requirement or measured improvement over this raw baseline.
