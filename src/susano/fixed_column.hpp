#pragma once

#include "susano/row_id.hpp"
#include "susano/validity_bitmap.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace susano {

template <typename T>
concept FixedColumnValue = std::same_as<T, std::int32_t> ||
                           std::same_as<T, std::int64_t> ||
                           std::same_as<T, double>;

template <FixedColumnValue T>
class FixedColumn {
public:
    using value_type = T;

    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

    // Access requires row.value() < size(). Debug builds assert this precondition;
    // release builds avoid a redundant branch on the hot read path.
    [[nodiscard]] bool is_null(RowId row) const noexcept {
        assert(row.value() < size());
        return !validity_.is_valid(row);
    }

    // Null rows return the deterministic T{} placeholder stored in values_.
    // Access has the same bounds precondition as is_null().
    [[nodiscard]] T value(RowId row) const noexcept {
        assert(row.value() < size());
        return values_[static_cast<std::size_t>(row.value())];
    }

    [[nodiscard]] std::span<const T> values() const noexcept {
        return values_;
    }

    [[nodiscard]] const ValidityBitmap& validity() const noexcept {
        return validity_;
    }

    void append(T value) {
        values_.push_back(value);
        try {
            validity_.append_valid();
        } catch (...) {
            values_.pop_back();
            throw;
        }
    }

    void append_null() {
        values_.push_back(T{});
        try {
            validity_.append_null();
        } catch (...) {
            values_.pop_back();
            throw;
        }
    }

private:
    // Invariant: values_.size() == validity_.size(). Null rows own a T{} value.
    std::vector<T> values_;
    ValidityBitmap validity_;
};

}  // namespace susano
