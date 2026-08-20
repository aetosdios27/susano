#pragma once

#include "susano/row_id.hpp"

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace susano {

enum class MainDeltaDomain : std::uint8_t {
    Main,
    Delta,
};

struct MainDeltaAddress {
    MainDeltaDomain domain;
    std::size_t offset;

    auto operator<=>(const MainDeltaAddress&) const = default;
};

[[nodiscard]] inline std::size_t main_delta_logical_size(std::size_t main_size,
                                                         std::size_t delta_size) noexcept {
    assert(delta_size <= std::numeric_limits<std::size_t>::max() - main_size);
    return main_size + delta_size;
}

// Precondition: row.value() < main_size + delta_size.
[[nodiscard]] inline MainDeltaAddress resolve_main_delta_row(RowId row,
                                                             std::size_t main_size,
                                                             [[maybe_unused]] std::size_t delta_size) noexcept {
    if (row.value() < main_size) {
        return MainDeltaAddress{MainDeltaDomain::Main, static_cast<std::size_t>(row.value())};
    }

    const auto delta_offset = row.value() - main_size;
    assert(delta_offset < delta_size);
    return MainDeltaAddress{MainDeltaDomain::Delta, static_cast<std::size_t>(delta_offset)};
}

}  // namespace susano
