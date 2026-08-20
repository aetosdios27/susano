#include "susano/main_delta_predicates.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

template <susano::FixedColumnValue T>
T value_from_state(std::uint64_t state) {
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(state % 101ULL) / 4.0;
    }
    return static_cast<T>(state % 101ULL);
}

template <susano::FixedColumnValue T>
std::vector<susano::RowId> reference_equal(const std::vector<std::optional<T>>& reference,
                                           T target) {
    std::vector<susano::RowId> matches;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        if (reference[index] && *reference[index] == target) {
            matches.emplace_back(static_cast<std::uint64_t>(index));
        }
    }
    return matches;
}

template <susano::FixedColumnValue T>
bool check_history(std::uint64_t seed) {
    susano::FixedColumn<T> initial;
    std::vector<std::optional<T>> reference;
    auto state = seed;
    for (std::size_t index = 0; index < 503; ++index) {
        state = state * 2862933555777941757ULL + 3037000493ULL;
        if (state % 17ULL == 0) {
            initial.append_null();
            reference.push_back(std::nullopt);
        } else {
            const auto value = value_from_state<T>(state);
            initial.append(value);
            reference.push_back(value);
        }
    }

    susano::MainDeltaColumn<T> column{initial};
    for (std::size_t operation = 0; operation < 5'003; ++operation) {
        state = state * 2862933555777941757ULL + 3037000493ULL;
        if (state % 13ULL == 0) {
            column.append_null();
            reference.push_back(std::nullopt);
        } else {
            const auto value = value_from_state<T>(state);
            column.append(value);
            reference.push_back(value);
        }

        if (operation % 97 == 0) {
            const auto target = value_from_state<T>(state >> 8U);
            if (susano::main_delta_equal(column, target) != reference_equal(reference, target)) {
                return false;
            }
        }
    }

    const std::vector<T> targets{value_from_state<T>(0), value_from_state<T>(50),
                                 value_from_state<T>(100), static_cast<T>(1'000)};
    for (const auto target : targets) {
        if (susano::main_delta_equal(column, target) != reference_equal(reference, target)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    if (!check_history<std::int32_t>(1) || !check_history<std::int32_t>(17) ||
        !check_history<std::int64_t>(29) || !check_history<double>(43)) {
        std::cerr << "main-delta equality differs from logical reference\n";
        return 1;
    }
    return 0;
}
