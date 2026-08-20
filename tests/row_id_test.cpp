#include "susano/row_id.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

namespace {

constexpr bool row_id_semantics_hold() {
    constexpr susano::RowId zero{0};
    constexpr susano::RowId one{1};
    constexpr susano::RowId another_one{1};
    constexpr susano::RowId large{std::numeric_limits<std::uint64_t>::max()};

    return zero.value() == 0 &&
           one.value() == 1 &&
           large.value() == std::numeric_limits<std::uint64_t>::max() &&
           one == another_one &&
           zero != one &&
           zero < one &&
           one < large &&
           large > zero;
}

static_assert(row_id_semantics_hold());

}  // namespace

int main() {
    if (!row_id_semantics_hold()) {
        std::cerr << "RowId construction, equality, or ordering is incorrect\n";
        return 1;
    }

    return 0;
}
