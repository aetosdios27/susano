#include "susano/fixed_column.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t row_count = 10'003;

template <typename T>
T generated_value(std::uint64_t state) {
    if constexpr (std::same_as<T, std::int32_t>) {
        const auto bounded = static_cast<std::int64_t>(state % 2'000'001ULL) - 1'000'000;
        return static_cast<std::int32_t>(bounded);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        const auto bounded = static_cast<std::int64_t>(state % 2'000'000'000'001ULL);
        return bounded - 1'000'000'000'000LL;
    } else {
        const auto bounded = static_cast<std::int64_t>(state % 2'000'001ULL) - 1'000'000;
        return static_cast<double>(bounded) / 8.0;
    }
}

template <typename T>
bool exactly_equal(T left, T right) {
    if constexpr (std::same_as<T, double>) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    } else {
        return left == right;
    }
}

bool required_null(std::size_t row) {
    return row == 0 || row == 63 || row == 64 || row == 65 || row == row_count - 1;
}

template <susano::FixedColumnValue T>
bool check_column(std::string_view type_name, std::uint64_t seed) {
    susano::FixedColumn<T> column;
    std::vector<T> expected_values;
    std::vector<bool> expected_nulls;
    expected_values.reserve(row_count);
    expected_nulls.reserve(row_count);
    auto state = seed;

    for (std::size_t index = 0; index < row_count; ++index) {
        state = state * 2862933555777941757ULL + 3037000493ULL;
        const auto value = generated_value<T>(state);
        const bool is_null = required_null(index) || (state % 23ULL == 0);

        if (is_null) {
            column.append_null();
            expected_values.push_back(T{});
        } else {
            column.append(value);
            expected_values.push_back(value);
        }
        expected_nulls.push_back(is_null);

        const auto expected_size = index + 1;
        if (column.size() != expected_size ||
            column.values().size() != expected_size ||
            column.validity().size() != expected_size) {
            std::cerr << type_name << " length invariant failed after row " << index << '\n';
            return false;
        }
    }

    const auto physical_values = column.values();
    for (std::size_t index = 0; index < row_count; ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (column.is_null(row) != expected_nulls[index]) {
            std::cerr << type_name << " nullness mismatch at row " << index << '\n';
            return false;
        }
        if (!exactly_equal(column.value(row), expected_values[index]) ||
            !exactly_equal(physical_values[index], expected_values[index])) {
            std::cerr << type_name << " value mismatch at row " << index << '\n';
            return false;
        }
    }

    return true;
}

}  // namespace

int main() {
    if (!check_column<std::int32_t>("int32", 0x123456789abcdef0ULL) ||
        !check_column<std::int64_t>("int64", 0x0fedcba987654321ULL) ||
        !check_column<double>("float64", 0x55aa55aa55aa55aaULL)) {
        return 1;
    }

    return 0;
}
