#pragma once

#include "susano/dictionary_column.hpp"
#include "susano/row_id.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace susano {

template <FixedColumnValue T, DictionaryCodeStorage Codes>
[[nodiscard]] std::vector<RowId> dictionary_equal(
    const BasicDictionaryColumn<T, Codes>& column,
    T target) {
    const auto target_id = column.dictionary().find(target);
    if (!target_id) {
        return {};
    }

    std::vector<RowId> matches;
    for (std::size_t index = 0; index < column.size(); ++index) {
        const RowId row{static_cast<std::uint64_t>(index)};
        if (!column.is_null(row) && column.value_id(row) == *target_id) {
            matches.push_back(row);
        }
    }
    return matches;
}

namespace detail {

template <FixedColumnValue T, DictionaryCodeStorage Codes>
[[nodiscard]] std::vector<RowId> codes_below(
    const BasicDictionaryColumn<T, Codes>& column,
    ValueId boundary) {
    std::vector<RowId> matches;
    for (std::size_t index = 0; index < column.size(); ++index) {
        const RowId row{static_cast<std::uint64_t>(index)};
        if (!column.is_null(row) && column.value_id(row) < boundary) {
            matches.push_back(row);
        }
    }
    return matches;
}

template <FixedColumnValue T, DictionaryCodeStorage Codes>
[[nodiscard]] std::vector<RowId> codes_at_least(
    const BasicDictionaryColumn<T, Codes>& column,
    ValueId boundary) {
    std::vector<RowId> matches;
    for (std::size_t index = 0; index < column.size(); ++index) {
        const RowId row{static_cast<std::uint64_t>(index)};
        if (!column.is_null(row) && column.value_id(row) >= boundary) {
            matches.push_back(row);
        }
    }
    return matches;
}

}  // namespace detail

template <FixedColumnValue T, DictionaryCodeStorage Codes>
[[nodiscard]] std::vector<RowId> dictionary_less(
    const BasicDictionaryColumn<T, Codes>& column,
    T target) {
    return detail::codes_below(column, column.dictionary().lower_bound_id(target));
}

template <FixedColumnValue T, DictionaryCodeStorage Codes>
[[nodiscard]] std::vector<RowId> dictionary_less_equal(
    const BasicDictionaryColumn<T, Codes>& column,
    T target) {
    return detail::codes_below(column, column.dictionary().upper_bound_id(target));
}

template <FixedColumnValue T, DictionaryCodeStorage Codes>
[[nodiscard]] std::vector<RowId> dictionary_greater(
    const BasicDictionaryColumn<T, Codes>& column,
    T target) {
    return detail::codes_at_least(column, column.dictionary().upper_bound_id(target));
}

template <FixedColumnValue T, DictionaryCodeStorage Codes>
[[nodiscard]] std::vector<RowId> dictionary_greater_equal(
    const BasicDictionaryColumn<T, Codes>& column,
    T target) {
    return detail::codes_at_least(column, column.dictionary().lower_bound_id(target));
}

}  // namespace susano
