#include "susano/delta_column.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

template <typename T>
bool exactly_equal(T left, T right) {
    if constexpr (std::same_as<T, double>) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    }
    return left == right;
}

template <susano::FixedColumnValue T>
T make_value(std::size_t index) {
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(index) / 16.0;
    }
    return static_cast<T>(index);
}

template <susano::FixedColumnValue T>
bool check_delta() {
    constexpr std::size_t rows = 10'003;
    susano::DeltaColumn<T> delta;
    if (delta.size() != 0 || delta.raw().size() != 0) {
        return false;
    }

    for (std::size_t index = 0; index < rows; ++index) {
        if (index % 2 == 0) {
            delta.append_null();
        } else {
            delta.append(make_value<T>(index));
        }
        if (delta.size() != index + 1 || delta.raw().size() != index + 1 ||
            delta.raw().validity().size() != index + 1 || delta.raw().values().size() != index + 1) {
            return false;
        }
    }

    for (std::size_t index = 0; index < rows; ++index) {
        const bool expected_null = index % 2 == 0;
        if (delta.is_null(index) != expected_null) {
            return false;
        }
        const auto expected = expected_null ? T{} : make_value<T>(index);
        if (!exactly_equal(delta.value(index), expected) ||
            !exactly_equal(delta.raw().values()[index], expected)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    susano::DeltaColumn<std::int32_t> one;
    one.append(42);
    if (one.size() != 1 || one.is_null(0) || one.value(0) != 42 ||
        !check_delta<std::int32_t>() || !check_delta<std::int64_t>() || !check_delta<double>()) {
        std::cerr << "append-only delta semantics failed\n";
        return 1;
    }
    return 0;
}
