#include "susano/fixed_column.hpp"
#include "susano/physical_type.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>

namespace {

constexpr std::uint64_t target_values_per_case = 50'000'000;

#if defined(__GNUC__) || defined(__clang__)
#define SUSANO_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define SUSANO_NOINLINE __declspec(noinline)
#else
#define SUSANO_NOINLINE
#endif

template <susano::FixedColumnValue T>
T value_for_row(std::size_t row) {
    if constexpr (std::same_as<T, std::int32_t>) {
        return static_cast<std::int32_t>(row % 1'000'003);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        return static_cast<std::int64_t>(row % 10'000'019);
    } else {
        return static_cast<double>(row % 1'000'003) * 0.5;
    }
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t scan_values(std::span<const T> values, std::uint64_t seed) {
    if constexpr (std::same_as<T, double>) {
        double sum = static_cast<double>(seed & 1023ULL);
        for (const auto value : values) {
            sum += value;
        }
        return std::bit_cast<std::uint64_t>(sum);
    } else {
        std::uint64_t checksum = seed;
        for (const auto value : values) {
            checksum += static_cast<std::uint64_t>(value);
        }
        return checksum;
    }
}

template <susano::FixedColumnValue T>
constexpr std::string_view type_name() {
    if constexpr (std::same_as<T, std::int32_t>) {
        return susano::physical_type_name(susano::PhysicalType::Int32);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        return susano::physical_type_name(susano::PhysicalType::Int64);
    } else {
        return susano::physical_type_name(susano::PhysicalType::Float64);
    }
}

template <susano::FixedColumnValue T>
void run_case(std::size_t rows) {
    susano::FixedColumn<T> column;
    for (std::size_t row = 0; row < rows; ++row) {
        column.append(value_for_row<T>(row));
    }

    const auto physical_values = column.values();
    const auto rows_u64 = static_cast<std::uint64_t>(rows);
    const auto iterations = std::max<std::uint64_t>(1, target_values_per_case / rows_u64);

    auto checksum = scan_values<T>(physical_values, 0);
    checksum ^= scan_values<T>(physical_values, 1);
    std::atomic_signal_fence(std::memory_order_seq_cst);

    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        checksum ^= scan_values<T>(physical_values, iteration + 2);
    }
    const auto finished = std::chrono::steady_clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);

    const auto elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
    const auto values_scanned = static_cast<double>(rows_u64) * static_cast<double>(iterations);
    const auto elapsed_seconds = static_cast<double>(elapsed_ns) / 1'000'000'000.0;
    const auto rows_per_second = values_scanned / elapsed_seconds;
    const auto bytes_per_second = rows_per_second * static_cast<double>(sizeof(T));
    const auto ns_per_value = static_cast<double>(elapsed_ns) / values_scanned;

    std::cout << type_name<T>() << ',' << rows << ',' << iterations << ',' << elapsed_ns << ','
              << rows_per_second << ',' << bytes_per_second << ',' << ns_per_value << ','
              << checksum << '\n';
}

bool parse_rows(std::string_view argument, std::size_t& rows) {
    std::uint64_t parsed = 0;
    const auto* first = argument.data();
    const auto* last = first + argument.size();
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last || parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    rows = static_cast<std::size_t>(parsed);
    return true;
}

void run_all_types(std::size_t rows) {
    run_case<std::int32_t>(rows);
    run_case<std::int64_t>(rows);
    run_case<double>(rows);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: susano_bench_fixed_column [rows]\n";
        return 2;
    }

    std::cout << "type,rows,iterations,elapsed_ns,rows_per_second,bytes_per_second,ns_per_value,checksum\n";
    if (argc == 2) {
        std::size_t rows = 0;
        if (!parse_rows(argv[1], rows)) {
            std::cerr << "rows must be a positive integer representable as size_t\n";
            return 2;
        }
        run_all_types(rows);
        return 0;
    }

    constexpr std::array<std::size_t, 4> row_counts{1'024, 100'000, 1'000'000, 10'000'000};
    for (const auto rows : row_counts) {
        run_all_types(rows);
    }
    return 0;
}
