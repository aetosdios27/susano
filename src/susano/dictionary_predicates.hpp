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

}  // namespace susano
