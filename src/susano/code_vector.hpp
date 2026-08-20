#pragma once

#include "susano/row_id.hpp"
#include "susano/sorted_dictionary.hpp"

#include <cassert>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace susano {

class CodeVector {
public:
    CodeVector() = default;

    explicit CodeVector(std::span<const ValueId> codes) : codes_{codes.begin(), codes.end()} {}

    explicit CodeVector(std::vector<ValueId> codes) noexcept : codes_{std::move(codes)} {}

    [[nodiscard]] std::size_t size() const noexcept {
        return codes_.size();
    }

    // Precondition: row.value() < size(). Debug builds assert this contract.
    [[nodiscard]] ValueId value(RowId row) const noexcept {
        assert(row.value() < size());
        return codes_[static_cast<std::size_t>(row.value())];
    }

    [[nodiscard]] std::span<const ValueId> values() const noexcept {
        return codes_;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return codes_.size() * sizeof(ValueId);
    }

private:
    std::vector<ValueId> codes_;
};

}  // namespace susano
