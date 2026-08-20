#include "susano/concurrent_delta.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

int main() {
    constexpr std::size_t row_count = 100'003;
    constexpr std::uint64_t logical_base = 50'000;
    susano::ConcurrentDeltaColumn<std::int64_t> delta;
    std::atomic<bool> writer_done{false};
    std::atomic<bool> failed{false};

    std::thread writer{[&] {
        for (std::size_t index = 0; index < row_count; ++index) {
            if (index % 7 == 0) {
                if (delta.append_null() != index) {
                    failed.store(true, std::memory_order_relaxed);
                }
            } else if (delta.append(static_cast<std::int64_t>(index * 3)) != index) {
                failed.store(true, std::memory_order_relaxed);
            }
        }
        writer_done.store(true, std::memory_order_release);
    }};

    std::size_t prior_size = 0;
    while (!writer_done.load(std::memory_order_acquire)) {
        const auto prefix = delta.snapshot(logical_base);
        if (prefix.size() < prior_size || prefix.logical_base() != logical_base) {
            failed.store(true, std::memory_order_relaxed);
            break;
        }
        prior_size = prefix.size();
        if (prefix.size() != 0) {
            const auto last = prefix.size() - 1;
            if (prefix.is_null(last) != (last % 7 == 0) ||
                (!prefix.is_null(last) &&
                 prefix.value(last) != static_cast<std::int64_t>(last * 3))) {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
        }
    }
    writer.join();

    const auto frozen = delta.freeze(logical_base);
    if (frozen->size() != row_count || frozen->logical_base() != logical_base || !delta.closed()) {
        failed.store(true, std::memory_order_relaxed);
    }
    for (std::size_t index = 0; index < row_count; ++index) {
        if (frozen->is_null(index) != (index % 7 == 0) ||
            (!frozen->is_null(index) &&
             frozen->value(index) != static_cast<std::int64_t>(index * 3))) {
            failed.store(true, std::memory_order_relaxed);
            break;
        }
    }

    try {
        static_cast<void>(delta.append(9));
        failed.store(true, std::memory_order_relaxed);
    } catch (const std::logic_error&) {
    }

    if (failed.load(std::memory_order_relaxed)) {
        std::cerr << "concurrent delta prefix, validity, or close semantics failed\n";
        return 1;
    }
    return 0;
}
