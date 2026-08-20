#pragma once

#include "susano/row_id.hpp"
#include "susano/sorted_dictionary.hpp"

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace susano {

[[nodiscard]] constexpr std::uint8_t code_bit_width(std::size_t cardinality) noexcept {
    if (cardinality <= 1) {
        return 0;
    }
    return static_cast<std::uint8_t>(std::bit_width(cardinality - 1));
}

class PackedCodeVector {
public:
    PackedCodeVector() = default;

    PackedCodeVector(std::uint8_t bit_width, std::span<const ValueId> codes)
        : size_{codes.size()}, bit_width_{bit_width} {
        if (bit_width_ > std::numeric_limits<ValueId>::digits) {
            throw std::invalid_argument{"packed code width must be between 0 and 32 bits"};
        }
        if (bit_width_ == 0) {
            for (const auto code : codes) {
                if (code != 0) {
                    throw std::invalid_argument{"zero-width packed codes can only store zero"};
                }
            }
            return;
        }
        if (size_ > std::numeric_limits<std::size_t>::max() / bit_width_) {
            throw std::length_error{"packed code bit count overflows size_t"};
        }

        const auto total_bits = size_ * bit_width_;
        const auto word_count = total_bits / bits_per_word + (total_bits % bits_per_word != 0 ? 1 : 0);
        words_.assign(word_count, 0);

        const auto value_mask = mask(bit_width_);
        for (std::size_t index = 0; index < size_; ++index) {
            const auto code = static_cast<std::uint64_t>(codes[index]);
            if ((code & ~value_mask) != 0) {
                throw std::invalid_argument{"dictionary code does not fit packed width"};
            }

            const auto bit_offset = index * bit_width_;
            const auto word_index = bit_offset / bits_per_word;
            const auto shift = static_cast<unsigned int>(bit_offset % bits_per_word);
            words_[word_index] |= code << shift;
            if (shift + bit_width_ > bits_per_word) {
                words_[word_index + 1] |= code >> (bits_per_word - shift);
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::uint8_t bit_width() const noexcept {
        return bit_width_;
    }

    [[nodiscard]] std::size_t word_count() const noexcept {
        return words_.size();
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return words_.size() * sizeof(std::uint64_t);
    }

    // Precondition: row.value() < size(). Debug builds assert this contract.
    [[nodiscard]] ValueId value(RowId row) const noexcept {
        assert(row.value() < size_);
        if (bit_width_ == 0) {
            return 0;
        }

        const auto index = static_cast<std::size_t>(row.value());
        const auto bit_offset = index * bit_width_;
        const auto word_index = bit_offset / bits_per_word;
        const auto shift = static_cast<unsigned int>(bit_offset % bits_per_word);
        auto code = words_[word_index] >> shift;
        if (shift + bit_width_ > bits_per_word) {
            code |= words_[word_index + 1] << (bits_per_word - shift);
        }
        return static_cast<ValueId>(code & mask(bit_width_));
    }

private:
    static constexpr std::size_t bits_per_word = std::numeric_limits<std::uint64_t>::digits;

    [[nodiscard]] static constexpr std::uint64_t mask(std::uint8_t bit_width) noexcept {
        if (bit_width == std::numeric_limits<ValueId>::digits) {
            return std::numeric_limits<ValueId>::max();
        }
        return bit_width == 0 ? 0 : (std::uint64_t{1} << bit_width) - 1;
    }

    std::vector<std::uint64_t> words_;
    std::size_t size_{0};
    std::uint8_t bit_width_{0};
};

}  // namespace susano
