#include "susano/main_delta_predicates.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

enum class Relation {
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

template <typename T>
bool compare(T value, T target, Relation relation) {
    switch (relation) {
        case Relation::Less:
            return value < target;
        case Relation::LessEqual:
            return value <= target;
        case Relation::Greater:
            return value > target;
        case Relation::GreaterEqual:
            return value >= target;
    }
    return false;
}

template <susano::FixedColumnValue T>
T value_from_state(std::uint64_t state) {
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(state % 211ULL) / 2.0;
    }
    return static_cast<T>(state % 211ULL);
}

template <susano::FixedColumnValue T>
std::vector<susano::RowId> reference_range(const std::vector<std::optional<T>>& reference,
                                           T target,
                                           Relation relation) {
    std::vector<susano::RowId> matches;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        if (reference[index] && compare(*reference[index], target, relation)) {
            matches.emplace_back(static_cast<std::uint64_t>(index));
        }
    }
    return matches;
}

template <susano::FixedColumnValue T>
std::vector<susano::RowId> encoded_range(const susano::MainDeltaColumn<T>& column,
                                         T target,
                                         Relation relation) {
    switch (relation) {
        case Relation::Less:
            return susano::main_delta_less(column, target);
        case Relation::LessEqual:
            return susano::main_delta_less_equal(column, target);
        case Relation::Greater:
            return susano::main_delta_greater(column, target);
        case Relation::GreaterEqual:
            return susano::main_delta_greater_equal(column, target);
    }
    return {};
}

template <susano::FixedColumnValue T>
bool check_all_ranges(const susano::MainDeltaColumn<T>& column,
                      const std::vector<std::optional<T>>& reference,
                      T target) {
    constexpr std::array<Relation, 4> relations{
        Relation::Less, Relation::LessEqual, Relation::Greater, Relation::GreaterEqual};
    for (const auto relation : relations) {
        if (encoded_range(column, target, relation) != reference_range(reference, target, relation)) {
            return false;
        }
    }
    return true;
}

template <susano::FixedColumnValue T>
bool check_history(std::uint64_t seed) {
    susano::FixedColumn<T> initial;
    std::vector<std::optional<T>> reference;
    auto state = seed;
    for (std::size_t index = 0; index < 509; ++index) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        if (state % 19ULL == 0) {
            initial.append_null();
            reference.push_back(std::nullopt);
        } else {
            const auto value = value_from_state<T>(state);
            initial.append(value);
            reference.push_back(value);
        }
    }

    susano::MainDeltaColumn<T> column{initial};
    for (std::size_t operation = 0; operation < 5'009; ++operation) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        if (state % 17ULL == 0) {
            column.append_null();
            reference.push_back(std::nullopt);
        } else {
            const auto value = value_from_state<T>(state);
            column.append(value);
            reference.push_back(value);
        }
        if (operation % 101 == 0 &&
            !check_all_ranges(column, reference, value_from_state<T>(state >> 8U))) {
            return false;
        }
    }

    const std::vector<T> targets{static_cast<T>(0), value_from_state<T>(100),
                                 static_cast<T>(500)};
    for (const auto target : targets) {
        if (!check_all_ranges(column, reference, target)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    if (!check_history<std::int32_t>(3) || !check_history<std::int32_t>(11) ||
        !check_history<std::int64_t>(23) || !check_history<double>(47)) {
        std::cerr << "main-delta ranges differ from logical reference\n";
        return 1;
    }
    return 0;
}
