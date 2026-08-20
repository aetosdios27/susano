#include "susano/generation_id.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

constexpr bool generation_id_semantics_hold() {
    constexpr susano::GenerationId zero{0};
    constexpr susano::GenerationId one{1};
    constexpr susano::GenerationId another_one{1};
    constexpr susano::GenerationId maximum{std::numeric_limits<std::uint64_t>::max()};
    return zero.value() == 0 && one.value() == 1 && one == another_one && zero < one &&
           maximum > one && maximum.value() == std::numeric_limits<std::uint64_t>::max();
}

static_assert(generation_id_semantics_hold());

int main() {
    if (!generation_id_semantics_hold()) {
        std::cerr << "generation identifier semantics failed\n";
        return 1;
    }
    return 0;
}
