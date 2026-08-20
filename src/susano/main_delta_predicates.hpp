#pragma once

#include "susano/dictionary_predicates.hpp"
#include "susano/main_delta_column.hpp"
#include "susano/row_id.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace susano {

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> main_delta_equal(const MainDeltaColumn<T>& column, T target) {
    auto matches = dictionary_equal(column.main().encoded(), target);
    for (std::size_t offset = 0; offset < column.delta_size(); ++offset) {
        if (!column.delta().is_null(offset) && column.delta().value(offset) == target) {
            matches.emplace_back(static_cast<std::uint64_t>(column.main_size() + offset));
        }
    }
    return matches;
}

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> main_delta_less(const MainDeltaColumn<T>& column, T target) {
    auto matches = dictionary_less(column.main().encoded(), target);
    for (std::size_t offset = 0; offset < column.delta_size(); ++offset) {
        if (!column.delta().is_null(offset) && column.delta().value(offset) < target) {
            matches.emplace_back(static_cast<std::uint64_t>(column.main_size() + offset));
        }
    }
    return matches;
}

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> main_delta_less_equal(const MainDeltaColumn<T>& column,
                                                       T target) {
    auto matches = dictionary_less_equal(column.main().encoded(), target);
    for (std::size_t offset = 0; offset < column.delta_size(); ++offset) {
        if (!column.delta().is_null(offset) && column.delta().value(offset) <= target) {
            matches.emplace_back(static_cast<std::uint64_t>(column.main_size() + offset));
        }
    }
    return matches;
}

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> main_delta_greater(const MainDeltaColumn<T>& column,
                                                    T target) {
    auto matches = dictionary_greater(column.main().encoded(), target);
    for (std::size_t offset = 0; offset < column.delta_size(); ++offset) {
        if (!column.delta().is_null(offset) && column.delta().value(offset) > target) {
            matches.emplace_back(static_cast<std::uint64_t>(column.main_size() + offset));
        }
    }
    return matches;
}

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> main_delta_greater_equal(const MainDeltaColumn<T>& column,
                                                          T target) {
    auto matches = dictionary_greater_equal(column.main().encoded(), target);
    for (std::size_t offset = 0; offset < column.delta_size(); ++offset) {
        if (!column.delta().is_null(offset) && column.delta().value(offset) >= target) {
            matches.emplace_back(static_cast<std::uint64_t>(column.main_size() + offset));
        }
    }
    return matches;
}

}  // namespace susano
