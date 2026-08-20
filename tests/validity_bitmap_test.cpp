#include "susano/validity_bitmap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool check_generated_pattern(std::size_t size) {
    susano::ValidityBitmap bitmap;
    std::vector<bool> reference;
    reference.reserve(size);
    std::uint64_t state = 0x4d595df4d0f33173ULL;

    if (bitmap.size() != 0 || bitmap.word_count() != 0) {
        std::cerr << "empty bitmap has storage or logical values\n";
        return false;
    }

    for (std::size_t index = 0; index < size; ++index) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const bool valid = (state & 7ULL) != 0;
        reference.push_back(valid);
        if (valid) {
            bitmap.append_valid();
        } else {
            bitmap.append_null();
        }

        const auto logical_size = index + 1;
        const auto expected_words =
            (logical_size + susano::ValidityBitmap::bits_per_word - 1) /
            susano::ValidityBitmap::bits_per_word;
        if (bitmap.size() != logical_size || bitmap.word_count() != expected_words) {
            std::cerr << "bitmap length invariant failed at size " << logical_size << '\n';
            return false;
        }
    }

    for (std::size_t index = 0; index < size; ++index) {
        if (bitmap.is_valid(susano::RowId{static_cast<std::uint64_t>(index)}) != reference[index]) {
            std::cerr << "generated pattern mismatch at index " << index
                      << " for size " << size << '\n';
            return false;
        }
    }

    return true;
}

bool check_explicit_boundaries() {
    constexpr std::size_t size = 130;
    constexpr std::array<std::size_t, 9> null_positions{0, 1, 62, 63, 64, 65, 127, 128, 129};
    susano::ValidityBitmap bitmap;

    for (std::size_t index = 0; index < size; ++index) {
        bool is_null = false;
        for (const auto position : null_positions) {
            is_null = is_null || index == position;
        }
        if (is_null) {
            bitmap.append_null();
        } else {
            bitmap.append_valid();
        }
    }

    for (std::size_t index = 0; index < size; ++index) {
        bool expected_valid = true;
        for (const auto position : null_positions) {
            expected_valid = expected_valid && index != position;
        }
        if (bitmap.is_valid(susano::RowId{static_cast<std::uint64_t>(index)}) != expected_valid) {
            std::cerr << "explicit boundary mismatch at index " << index << '\n';
            return false;
        }
    }

    return bitmap.size() == size && bitmap.word_count() == 3;
}

}  // namespace

int main() {
    constexpr std::array<std::size_t, 10> sizes{0, 1, 63, 64, 65, 127, 128, 129, 10'000, 10'003};

    if (!check_explicit_boundaries()) {
        return 1;
    }
    for (const auto size : sizes) {
        if (!check_generated_pattern(size)) {
            return 1;
        }
    }

    return 0;
}
