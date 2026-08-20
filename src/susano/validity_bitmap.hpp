#pragma once

#include "susano/row_id.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace susano {

class ValidityBitmap {
public:
    using Word = std::uint64_t;
    static constexpr std::size_t bits_per_word = std::numeric_limits<Word>::digits;

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::size_t word_count() const noexcept {
        return words_.size();
    }

    // Precondition: row.value() < size(). Debug builds assert this contract.
    [[nodiscard]] bool is_valid(RowId row) const noexcept {
        assert(row.value() < size_);
        const auto word_index = static_cast<std::size_t>(row.value() / bits_per_word);
        const auto bit_index = static_cast<unsigned int>(row.value() % bits_per_word);
        return (words_[word_index] & (Word{1} << bit_index)) != 0;
    }

    void append_valid() {
        append(true);
    }

    void append_null() {
        append(false);
    }

private:
    void append(bool valid) {
        const auto bit_index = size_ % bits_per_word;
        if (bit_index == 0) {
            words_.push_back(0);
        }
        if (valid) {
            words_.back() |= Word{1} << static_cast<unsigned int>(bit_index);
        }
        ++size_;
    }

    std::vector<Word> words_;
    std::size_t size_{0};
};

}  // namespace susano
