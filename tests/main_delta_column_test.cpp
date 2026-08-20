#include "susano/main_delta_column.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

template <typename T>
bool exactly_equal(T left, T right) {
    if constexpr (std::same_as<T, double>) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    }
    return left == right;
}

template <susano::FixedColumnValue T>
T generated_value(std::uint64_t state) {
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(state % 1009ULL) / 8.0;
    }
    return static_cast<T>(state % 1009ULL);
}

template <susano::FixedColumnValue T>
bool check_reference(const susano::MainDeltaColumn<T>& column,
                     const std::vector<std::optional<T>>& reference) {
    if (column.size() != reference.size() ||
        column.size() != column.main_size() + column.delta_size()) {
        return false;
    }
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (column.is_null(row) != !reference[index].has_value()) {
            return false;
        }
        if (reference[index] && !exactly_equal(column.value(row), *reference[index])) {
            return false;
        }
    }
    return true;
}

template <susano::FixedColumnValue T>
bool check_history(std::uint64_t seed) {
    constexpr std::size_t initial_rows = 257;
    constexpr std::size_t operations = 5'003;
    susano::FixedColumn<T> initial;
    std::vector<std::optional<T>> reference;
    reference.reserve(initial_rows + operations);
    auto state = seed;

    for (std::size_t index = 0; index < initial_rows; ++index) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        if (state % 13ULL == 0) {
            initial.append_null();
            reference.push_back(std::nullopt);
        } else {
            const auto value = generated_value<T>(state);
            initial.append(value);
            reference.push_back(value);
        }
    }

    susano::MainDeltaColumn<T> column{initial};
    const auto dictionary_before = std::vector<T>{column.main().encoded().dictionary().values().begin(),
                                                  column.main().encoded().dictionary().values().end()};
    const auto codes_before = std::vector<susano::ValueId>{column.main().encoded().codes().values().begin(),
                                                           column.main().encoded().codes().values().end()};
    std::vector<bool> validity_before;
    validity_before.reserve(column.main_size());
    for (std::size_t index = 0; index < column.main_size(); ++index) {
        validity_before.push_back(!column.main().is_null(
            susano::RowId{static_cast<std::uint64_t>(index)}));
    }

    for (std::size_t operation = 0; operation < operations; ++operation) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        if (state % 11ULL == 0) {
            column.append_null();
            reference.push_back(std::nullopt);
        } else {
            const auto value = generated_value<T>(state);
            column.append(value);
            reference.push_back(value);
        }

        if (operation % 37 == 0) {
            const auto index = static_cast<std::size_t>((state >> 16U) % reference.size());
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            if (column.is_null(row) != !reference[index].has_value() ||
                (reference[index] && !exactly_equal(column.value(row), *reference[index]))) {
                return false;
            }
        }
    }

    if (!check_reference(column, reference) || dictionary_before.size() != column.main().encoded().dictionary().size() ||
        codes_before.size() != column.main().encoded().codes().size()) {
        return false;
    }
    for (std::size_t index = 0; index < dictionary_before.size(); ++index) {
        if (!exactly_equal(dictionary_before[index],
                           column.main().encoded().dictionary().value(static_cast<susano::ValueId>(index)))) {
            return false;
        }
    }
    for (std::size_t index = 0; index < codes_before.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (codes_before[index] != column.main().encoded().value_id(row) ||
            validity_before[index] != !column.main().is_null(row)) {
            return false;
        }
    }
    return true;
}

bool check_explicit_boundaries() {
    susano::FixedColumn<std::int32_t> initial;
    initial.append(10);
    initial.append_null();
    initial.append(30);
    susano::MainDeltaColumn<std::int32_t> column{initial};
    column.append(40);
    column.append_null();
    column.append(60);

    return column.main_size() == 3 && column.delta_size() == 3 && column.size() == 6 &&
           column.value(susano::RowId{0}) == 10 && column.is_null(susano::RowId{1}) &&
           column.value(susano::RowId{2}) == 30 && column.value(susano::RowId{3}) == 40 &&
           column.is_null(susano::RowId{4}) && column.value(susano::RowId{5}) == 60;
}

}  // namespace

int main() {
    susano::FixedColumn<std::int32_t> empty_initial;
    susano::MainDeltaColumn<std::int32_t> delta_only{empty_initial};
    const bool started_empty = delta_only.size() == 0 && delta_only.main_size() == 0 &&
                               delta_only.delta_size() == 0;
    delta_only.append(7);

    if (!started_empty || delta_only.main_size() != 0 || delta_only.delta_size() != 1 ||
        delta_only.value(susano::RowId{0}) != 7 || !check_explicit_boundaries() ||
        !check_history<std::int32_t>(1) || !check_history<std::int32_t>(7) ||
        !check_history<std::int64_t>(19) || !check_history<double>(41)) {
        std::cerr << "unified main-delta reads, boundaries, or lifecycle invariants failed\n";
        return 1;
    }
    return 0;
}
