#pragma once

#include "susano/delta_column.hpp"
#include "susano/fixed_column.hpp"
#include "susano/main_column.hpp"
#include "susano/main_delta_address.hpp"
#include "susano/row_id.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace susano {

template <FixedColumnValue T>
class MainDeltaColumn {
public:
    explicit MainDeltaColumn(const FixedColumn<T>& initial) : main_{initial} {}

    explicit MainDeltaColumn(MainColumn<T> main) : main_{std::move(main)} {}

    [[nodiscard]] std::size_t size() const noexcept {
        return main_delta_logical_size(main_.size(), delta_.size());
    }

    [[nodiscard]] std::size_t main_size() const noexcept {
        return main_.size();
    }

    [[nodiscard]] std::size_t delta_size() const noexcept {
        return delta_.size();
    }

    [[nodiscard]] bool is_null(RowId row) const noexcept {
        assert(row.value() < size());
        const auto address = resolve_main_delta_row(row, main_.size(), delta_.size());
        if (address.domain == MainDeltaDomain::Main) {
            return main_.is_null(RowId{static_cast<std::uint64_t>(address.offset)});
        }
        return delta_.is_null(address.offset);
    }

    // Preconditions: row.value() < size() and !is_null(row).
    [[nodiscard]] T value(RowId row) const noexcept {
        assert(row.value() < size());
        const auto address = resolve_main_delta_row(row, main_.size(), delta_.size());
        if (address.domain == MainDeltaDomain::Main) {
            return main_.value(RowId{static_cast<std::uint64_t>(address.offset)});
        }
        return delta_.value(address.offset);
    }

    void append(T value) {
        delta_.append(value);
    }

    void append_null() {
        delta_.append_null();
    }

    [[nodiscard]] const MainColumn<T>& main() const noexcept {
        return main_;
    }

    [[nodiscard]] const DeltaColumn<T>& delta() const noexcept {
        return delta_;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return main_.storage_bytes() + delta_.storage_bytes();
    }

private:
    MainColumn<T> main_;
    DeltaColumn<T> delta_;
};

}  // namespace susano
