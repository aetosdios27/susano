# Susano

Susano is a from-scratch, high-performance in-memory analytical database under active development. The project studies database systems ideas directly and will keep its implementation understandable, measurable, and independently testable.

The repository currently contains only a C++23 executable and a CTest smoke test. No query language, storage engine, catalog, or execution machinery has been implemented yet.

## Requirements

- CMake 3.25 or newer
- A C++23 compiler (GCC and Clang are supported targets)

## Build and Test

Configure and build the default development tree:

```sh
cmake -S . -B build
cmake --build build
```

Run the complete test suite through CTest:

```sh
ctest --test-dir build --output-on-failure
```

To build with AddressSanitizer and UndefinedBehaviorSanitizer enabled:

```sh
cmake -S . -B build-sanitized -DSUSANO_ENABLE_SANITIZERS=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

The normal build does not enable sanitizers. Keep performance-sensitive choices backed by benchmarks and correctness tests as the system grows.
