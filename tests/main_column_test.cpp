#include "susano/main_column.hpp"

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
        return static_cast<double>(index % 1009) / 8.0;
    }
    return static_cast<T>(index % 1009);
}

template <susano::FixedColumnValue T>
bool check_main(std::size_t rows) {
    susano::FixedColumn<T> source;
    for (std::size_t index = 0; index < rows; ++index) {
        if (index == 0 || index + 1 == rows || index % 17 == 0) {
            source.append_null();
        } else {
            source.append(make_value<T>(index));
        }
    }

    const susano::MainColumn<T> main{source};
    if (main.size() != source.size() || main.encoded().size() != source.size() ||
        main.encoded().codes().size() != source.size() ||
        main.encoded().validity().size() != source.size()) {
        return false;
    }
    for (std::size_t index = 0; index < rows; ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (main.is_null(row) != source.is_null(row)) {
            return false;
        }
        if (!source.is_null(row) && !exactly_equal(main.value(row), source.value(row))) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    susano::FixedColumn<std::int32_t> empty_source;
    const susano::MainColumn<std::int32_t> empty{empty_source};
    susano::FixedColumn<std::int32_t> one_source;
    one_source.append(7);
    const susano::MainColumn<std::int32_t> one{one_source};

    if (empty.size() != 0 || one.size() != 1 || one.value(susano::RowId{0}) != 7 ||
        !check_main<std::int32_t>(100'003) || !check_main<std::int64_t>(10'003) ||
        !check_main<double>(10'003)) {
        std::cerr << "immutable main reads or fixed-ID representation failed\n";
        return 1;
    }
    return 0;
}
