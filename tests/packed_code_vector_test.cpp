#include "susano/packed_code_vector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

bool check_cardinality_widths() {
    constexpr std::array<std::pair<std::size_t, std::uint8_t>, 26> cases{{
        {0, 0},       {1, 0},       {2, 1},       {3, 2},       {4, 2},
        {7, 3},       {8, 3},       {15, 4},      {16, 4},      {31, 5},
        {32, 5},      {63, 6},      {64, 6},      {127, 7},     {128, 7},
        {255, 8},     {256, 8},     {257, 9},     {511, 9},     {512, 9},
        {1023, 10},   {1024, 10},   {65'535, 16}, {65'536, 16}, {65'537, 17},
        {1ULL << 32U, 32},
    }};
    for (const auto& [cardinality, expected] : cases) {
        if (susano::code_bit_width(cardinality) != expected) {
            std::cerr << "wrong code width for cardinality " << cardinality << '\n';
            return false;
        }
    }
    return true;
}

bool check_width(std::uint8_t width) {
    constexpr std::size_t value_count = 10'007;
    const std::uint64_t mask = width == 32 ? 0xffff'ffffULL : (std::uint64_t{1} << width) - 1;
    std::vector<susano::ValueId> expected;
    expected.reserve(value_count);
    std::uint64_t state = 0x243f6a8885a308d3ULL ^ width;
    for (std::size_t index = 0; index < value_count; ++index) {
        state = state * 2862933555777941757ULL + 3037000493ULL;
        expected.push_back(static_cast<susano::ValueId>(state & mask));
    }
    expected.front() = 0;
    expected.back() = static_cast<susano::ValueId>(mask);

    const susano::PackedCodeVector packed{width, expected};
    const auto total_bits = value_count * width;
    const auto expected_words = total_bits / 64 + (total_bits % 64 != 0 ? 1 : 0);
    if (packed.size() != value_count || packed.bit_width() != width ||
        packed.word_count() != expected_words || packed.storage_bytes() != expected_words * 8) {
        std::cerr << "packed storage accounting failed for width " << static_cast<unsigned int>(width) << '\n';
        return false;
    }

    for (std::size_t index = 0; index < value_count; ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (packed.value(row) != expected[index]) {
            std::cerr << "packed round trip failed for width " << static_cast<unsigned int>(width)
                      << " at index " << index << '\n';
            return false;
        }
    }
    return true;
}

bool check_invalid_inputs() {
    try {
        const std::array<susano::ValueId, 1> one{1};
        const susano::PackedCodeVector invalid{0, one};
        static_cast<void>(invalid);
        return false;
    } catch (const std::invalid_argument&) {
    }
    try {
        const std::array<susano::ValueId, 1> empty_code{0};
        const susano::PackedCodeVector invalid{33, empty_code};
        static_cast<void>(invalid);
        return false;
    } catch (const std::invalid_argument&) {
    }
    try {
        const std::array<susano::ValueId, 1> too_large{8};
        const susano::PackedCodeVector invalid{3, too_large};
        static_cast<void>(invalid);
        return false;
    } catch (const std::invalid_argument&) {
    }
    return true;
}

}  // namespace

int main() {
    constexpr std::array<std::uint8_t, 14> widths{1, 2, 3, 4, 5, 7, 8, 9, 10, 15, 16, 17, 31, 32};
    for (const auto width : widths) {
        if (!check_width(width)) {
            return 1;
        }
    }

    const std::vector<susano::ValueId> zeros(10'007, 0);
    const susano::PackedCodeVector zero_width{0, zeros};
    if (zero_width.size() != zeros.size() || zero_width.word_count() != 0 ||
        zero_width.value(susano::RowId{10'006}) != 0 || !check_cardinality_widths() ||
        !check_invalid_inputs()) {
        std::cerr << "zero-width, cardinality, or validation behavior failed\n";
        return 1;
    }
    return 0;
}
