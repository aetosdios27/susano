#pragma once

#include "susano/fixed_column.hpp"
#include "susano/row_id.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace susano {

template <FixedColumnValue T>
class DeltaColumn {
public:
    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

    void append(T value) {
        values_.append(value);
    }

    void append_null() {
        values_.append_null();
    }

    // The offset is delta-local, not a logical RowId.
    [[nodiscard]] bool is_null(std::size_t offset) const noexcept {
        assert(offset < size());
        return values_.is_null(RowId{static_cast<std::uint64_t>(offset)});
    }

    // The offset is delta-local. Null rows return the raw T{} placeholder.
    [[nodiscard]] T value(std::size_t offset) const noexcept {
        assert(offset < size());
        return values_.value(RowId{static_cast<std::uint64_t>(offset)});
    }

    [[nodiscard]] const FixedColumn<T>& raw() const noexcept {
        return values_;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return values_.values().size_bytes() +
               values_.validity().word_count() * sizeof(ValidityBitmap::Word);
    }

private:
    FixedColumn<T> values_;
};

}  // namespace susano
