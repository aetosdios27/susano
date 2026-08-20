#include "susano/code_vector.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

int main() {
    std::vector<susano::ValueId> expected;
    expected.reserve(100'003);
    std::uint64_t state = 0x6a09e667f3bcc909ULL;
    for (std::size_t index = 0; index < 100'003; ++index) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        expected.push_back(static_cast<susano::ValueId>(state));
    }
    expected.front() = 0;
    expected.back() = std::numeric_limits<susano::ValueId>::max();

    const susano::CodeVector codes{expected};
    if (codes.size() != expected.size() ||
        codes.storage_bytes() != expected.size() * sizeof(susano::ValueId) ||
        codes.values().size() != expected.size()) {
        std::cerr << "fixed code vector size accounting failed\n";
        return 1;
    }

    for (std::size_t index = 0; index < expected.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (codes.value(row) != expected[index] || codes.values()[index] != expected[index]) {
            std::cerr << "fixed code vector round trip failed at " << index << '\n';
            return 1;
        }
    }

    const std::vector<susano::ValueId> empty_values;
    const susano::CodeVector empty{empty_values};
    if (empty.size() != 0 || empty.storage_bytes() != 0) {
        std::cerr << "empty fixed code vector is not empty\n";
        return 1;
    }
    return 0;
}
