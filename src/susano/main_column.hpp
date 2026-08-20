#pragma once

#include "susano/dictionary_column.hpp"
#include "susano/fixed_column.hpp"
#include "susano/row_id.hpp"

#include <cstddef>

namespace susano {

template <FixedColumnValue T>
class MainColumn {
public:
    explicit MainColumn(const FixedColumn<T>& source) : encoded_{source} {}

    [[nodiscard]] std::size_t size() const noexcept {
        return encoded_.size();
    }

    [[nodiscard]] bool is_null(RowId row) const noexcept {
        return encoded_.is_null(row);
    }

    // Preconditions: row.value() < size() and !is_null(row).
    [[nodiscard]] const T& value(RowId row) const noexcept {
        return encoded_.value(row);
    }

    [[nodiscard]] const DictionaryColumn<T>& encoded() const noexcept {
        return encoded_;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return encoded_.storage_bytes();
    }

private:
    DictionaryColumn<T> encoded_;
};

}  // namespace susano
