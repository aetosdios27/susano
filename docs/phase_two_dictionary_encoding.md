# Phase 2 Dictionary Encoding

Phase 2 adds immutable sorted dictionaries, fixed and packed value IDs, and scalar predicates that stay on encoded IDs. It is an experimental representation, not a general compression framework or Susano's final main-store format.

## Representation

### Sorted dictionary

`SortedDictionary<T>` supports `std::int32_t`, `std::int64_t`, and `double`. Construction copies non-null values, sorts them, removes duplicates, and assigns `ValueId` values in ascending dictionary order. `ValueId` is `std::uint32_t`; ID zero names the smallest dictionary value and IDs preserve physical value ordering.

Construction and lookup use binary search. No unordered-container iteration participates in ID assignment, so identical logical input produces identical dictionary values and IDs.

Floating dictionaries accept finite values only. NaN and positive or negative infinity are rejected. Positive and negative zero are canonicalized to positive zero before sorting and lookup. This gives the supported finite domain one deterministic order without inventing NaN semantics.

### Codes and nulls

`DictionaryColumn<T>` stores one fixed `std::uint32_t` code per row. `PackedDictionaryColumn<T>` stores the same IDs consecutively in 64-bit words using the minimum width for the dictionary cardinality:

```text
C <= 1: 0 bits
C > 1:  bit_width(C - 1)
```

A code can straddle two words. Extraction combines the low part of the first word with the high part of the next word and then masks to the configured width. Widths are limited to 0 through 32 bits. Zero-width vectors represent only ID zero and allocate no payload words.

Null is not a dictionary member. Both column variants retain a separate `ValidityBitmap`. A null row stores the code-zero placeholder, but the code is not logically observable. Therefore a null never compares equal to the smallest dictionary value.

Both encoded columns are immutable after construction. They expose the dictionary, code storage, and validity bitmap as read-only objects.

## Invariants

The implementation and tests enforce:

- every non-null logical value maps to exactly one dictionary ID;
- dictionary values are unique, ascending, and deterministic;
- dictionary IDs preserve dictionary ordering;
- every valid stored code is less than dictionary cardinality;
- logical row count equals code count and validity count;
- nulls are absent from the dictionary and carry a clear validity bit;
- null rows use a deterministic code-zero placeholder;
- fixed and packed columns decode to the same logical values and IDs;
- packed widths use `ceil(log2(C))`, with zero bits for cardinalities zero and one;
- packed extraction performs no shift by 64 and no final-element over-read;
- equality predicates look up one target ID and compare row codes;
- range predicates obtain lower or upper dictionary boundaries once and compare row codes;
- all predicate outputs exclude nulls and exactly match decoded references.

Property tests cover widths 1, 2, 3, 4, 5, 7, 8, 9, 10, 15, 16, 17, 31, and 32 across word boundaries. Cardinality tests cover zero, one, every requested power-of-two boundary through 65,537, and the 32-bit limit. Dictionary-column tests encode more than 100,000 deterministic rows for every supported type and both code stores.

## Benchmark methodology

Build the release benchmarks:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSUSANO_BUILD_BENCHMARKS=ON
cmake --build build-release
```

Run one controlled dataset:

```sh
./build-release/susano_bench_dictionary \
    --rows 1000000 --cardinality 1000 --null-permille 0 \
    --distribution uniform --sortedness random --type int64 --seed 42
```

Run the required cardinality and code-width sweeps:

```sh
./build-release/susano_bench_dictionary --sweep cardinality > dictionary.csv
./build-release/susano_bench_packed_codes > packed_codes.csv
python3 benchmarks/summarize_dictionary.py dictionary.csv packed_codes.csv
```

The cardinality sweep uses one million rows, cardinalities 10, 100, 1,000, 10,000, 100,000, and 1,000,000, all three physical types, and both uniform and deterministic skewed distributions. The skewed generator chooses the smaller of two uniform IDs. The first `C` rows force every requested ID to occur, making actual cardinality deterministic. The required sweep has no nulls; the single-case interface separately controls null fraction in permille and logical sortedness.

Data generation is outside all reported build and operation timings. Raw, fixed-ID, and packed-ID builds are timed separately. Each operation runs one explicit warmup and one measured pass. Checksums or result counts are compared outside the timed region before output is accepted. Random lookup uses 100,003 deterministic row indices. CSV output includes dataset parameters, all representation components, total bytes, build time, operation throughput, nanoseconds per value, selectivity, and checksum. Hardware cycles per value are `nan` unless an external counter run supplies them.

Measurements below were recorded on an Intel Core Ultra 9 185H, x86-64 Linux, GCC 16.1.1, CMake Release. CPU affinity and frequency were not pinned. These are single-run observations, not general performance claims.

## Results

### Representation size and decoded scan

Uniform one-million-row cases:

| Type | C/N | Raw B/value | uint32 dictionary B/value | Packed dictionary B/value | Raw decoded ns/value | uint32 decoded ns/value | Packed decoded ns/value |
|---|---:|---:|---:|---:|---:|---:|---:|
| int32 | 0.00001 | 4.125 | 4.125 | 0.625 | 0.435 | 1.230 | 3.300 |
| int32 | 0.001 | 4.125 | 4.129 | 1.379 | 0.452 | 1.249 | 3.854 |
| int32 | 0.1 | 4.125 | 4.525 | 2.650 | 0.672 | 1.171 | 3.666 |
| int32 | 1.0 | 4.125 | 8.125 | 6.625 | 0.436 | 1.214 | 3.398 |
| int64 | 0.00001 | 8.125 | 4.125 | 0.625 | 0.680 | 1.347 | 3.366 |
| int64 | 0.001 | 8.125 | 4.133 | 1.383 | 0.516 | 1.448 | 4.276 |
| int64 | 0.1 | 8.125 | 4.925 | 3.050 | 0.517 | 1.497 | 3.480 |
| int64 | 1.0 | 8.125 | 12.125 | 10.625 | 0.585 | 1.377 | 3.446 |
| float64 | 0.00001 | 8.125 | 4.125 | 0.625 | 1.179 | 1.060 | 3.301 |
| float64 | 0.001 | 8.125 | 4.133 | 1.383 | 1.082 | 1.226 | 3.889 |
| float64 | 0.1 | 8.125 | 4.925 | 3.050 | 1.280 | 1.217 | 3.442 |
| float64 | 1.0 | 8.125 | 12.125 | 10.625 | 1.534 | 1.408 | 3.584 |

Measured size crossovers:

- Packed dictionaries were smaller than raw storage through `C/N = 0.1` for all three types and larger at `C/N = 1.0`. The measured crossover therefore lies between those sampled ratios.
- Fixed `uint32_t` IDs never reduced `int32` size: their code payload is already the raw value width and the dictionary adds bytes.
- Fixed IDs reduced `int64` and `float64` size through `C/N = 0.1` and lost at `C/N = 1.0`.

Packed decoded scans lost to raw scans in all 36 cardinality/type/distribution configurations, by 2.34 to 16.30 times in this run. Fixed-ID decoded scans won 5 of 36 configurations; those isolated wins were small enough that they are not evidence of a general crossover.

### Encoded predicates

Across the 36 cardinality configurations:

- fixed-ID equality over codes beat decoded equality in 28 cases; measured time ratios ranged from 0.48 to 1.35;
- fixed-ID ordered range predicates beat decoded ranges in 31 cases; ratios ranged from 0.50 to 1.15;
- packed-code equality and range predicates lost every case because scalar extraction cost dominated the reduced code traffic.

At uniform `C = 1,000`:

| Type | Decoded equality ns/value | uint32 encoded equality | Packed encoded equality | Decoded range | uint32 encoded range |
|---|---:|---:|---:|---:|---:|
| int32 | 1.527 | 1.258 | 3.971 | 1.427 | 1.347 |
| int64 | 1.664 | 1.416 | 4.169 | 1.662 | 1.514 |
| float64 | 2.490 | 1.387 | 3.699 | 2.317 | 1.444 |

Keeping fixed-ID execution encoded produced the clearest Phase 2 query benefit, especially for `double`. Packing those same IDs saved memory but erased that scalar predicate benefit.

### Packed versus uint32 codes

The synthetic one-million-code sweep measured every width from 1 through 32. Representative points:

| Width | Packed B/value | uint32 scan ns/value | Packed scan ns/value | uint32 random ns/value | Packed random ns/value |
|---:|---:|---:|---:|---:|---:|
| 1 | 0.125 | 0.479 | 3.494 | 5.041 | 2.201 |
| 4 | 0.500 | 0.305 | 1.382 | 1.884 | 1.762 |
| 8 | 1.000 | 0.189 | 1.298 | 1.754 | 1.995 |
| 10 | 1.250 | 0.197 | 1.606 | 1.844 | 4.415 |
| 16 | 2.000 | 0.185 | 1.277 | 1.708 | 2.244 |
| 24 | 3.000 | 0.199 | 1.423 | 1.667 | 6.468 |
| 32 | 4.000 | 0.209 | 1.580 | 1.961 | 2.238 |

Packed sequential scans lost at every width, by 2.98 to 10.56 times. Packed random lookup won only at widths 1 through 4 in this run; at width 1 it used 1/32 of the bytes and took 0.44 times the uint32 lookup time. From width 5 onward scalar unpacking outweighed the measured cache benefit.

### Construction cost

For uniform `int64`, one million rows, and `C = 1,000`, raw construction took 12.9 ms, the fixed dictionary build took 168.0 ms, and the packed dictionary build took 170.5 ms. Dictionary creation cost therefore matters independently of scan cost and must be amortized by immutable use.

### Representative performance counters

Command:

```sh
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses \
    ./build-release/susano_bench_dictionary \
    --rows 1000000 --cardinality 1000 --null-permille 0 \
    --distribution uniform --sortedness random --type int64 --seed 42
```

Linux reported hybrid-core counters separately. The performance-core counts, covering 99.99% of the reported run, were:

| Event | Count |
|---|---:|
| cycles | 775,904,580 |
| instructions | 1,038,102,003 |
| cache references | 4,685,076 |
| cache misses | 1,478,252 |
| branches | 208,627,768 |
| branch misses | 20,406,740 |

These process-level counters include generation, all three builds, warmups, every measured operation, validation, and CSV output. They are not per-operation cycle counts.

## Open questions for Phase 3

- Immutable dictionary build cost is much larger than raw append cost. A future main/delta merge must amortize it rather than rebuilding on each write.
- Fixed IDs preserve most encoded-predicate benefit and avoid packed extraction cost, but use more memory. A future main representation may need both choices; Phase 2 does not add adaptive selection.
- Packed IDs are attractive for very low cardinality and showed a random-lookup benefit only at 1 to 4 bits. That crossover should influence, not dictate, later main-store layout decisions.
- Ordered IDs materially helped scalar equality and range predicates. A future delta representation must define how queries compare or translate IDs across immutable and mutable domains.
- Null validity remains independent and can be shared conceptually across raw, fixed-ID, and packed-ID representations.

No main store, delta store, merge path, RLE, frame-of-reference encoding, SIMD kernel, SQL layer, MVCC, WAL, or generic codec framework is implemented here.
