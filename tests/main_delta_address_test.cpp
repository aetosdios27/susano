#include "susano/main_delta_address.hpp"

#include <cstddef>
#include <iostream>

namespace {

bool check_address(susano::RowId row,
                   std::size_t main_size,
                   std::size_t delta_size,
                   susano::MainDeltaDomain expected_domain,
                   std::size_t expected_offset) {
    const auto address = susano::resolve_main_delta_row(row, main_size, delta_size);
    return address.domain == expected_domain && address.offset == expected_offset;
}

}  // namespace

int main() {
    using susano::MainDeltaDomain;

    if (susano::main_delta_logical_size(0, 0) != 0 ||
        susano::main_delta_logical_size(1, 0) != 1 ||
        susano::main_delta_logical_size(0, 1) != 1 ||
        susano::main_delta_logical_size(64, 65) != 129 ||
        !check_address(susano::RowId{0}, 0, 1, MainDeltaDomain::Delta, 0) ||
        !check_address(susano::RowId{0}, 1, 0, MainDeltaDomain::Main, 0) ||
        !check_address(susano::RowId{0}, 1, 1, MainDeltaDomain::Main, 0) ||
        !check_address(susano::RowId{1}, 1, 1, MainDeltaDomain::Delta, 0) ||
        !check_address(susano::RowId{63}, 64, 65, MainDeltaDomain::Main, 63) ||
        !check_address(susano::RowId{64}, 64, 65, MainDeltaDomain::Delta, 0) ||
        !check_address(susano::RowId{128}, 64, 65, MainDeltaDomain::Delta, 64)) {
        std::cerr << "main-delta row boundary mapping failed\n";
        return 1;
    }
    return 0;
}
