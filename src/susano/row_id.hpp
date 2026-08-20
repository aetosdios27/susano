#pragma once

#include <compare>
#include <cstdint>

namespace susano {

class RowId {
public:
    explicit constexpr RowId(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    auto operator<=>(const RowId&) const = default;

private:
    std::uint64_t value_;
};

}  // namespace susano
