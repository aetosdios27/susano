#pragma once

#include "susano/code_vector.hpp"
#include "susano/fixed_column.hpp"
#include "susano/packed_code_vector.hpp"
#include "susano/sorted_dictionary.hpp"
#include "susano/validity_bitmap.hpp"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <stdexcept>
#include <vector>

namespace susano {

template <typename Codes>
concept DictionaryCodeStorage = std::same_as<Codes, CodeVector> ||
                                std::same_as<Codes, PackedCodeVector>;

template <FixedColumnValue T, DictionaryCodeStorage Codes>
class BasicDictionaryColumn {
public:
    using value_type = T;
    using code_storage_type = Codes;

    explicit BasicDictionaryColumn(const FixedColumn<T>& source)
        : BasicDictionaryColumn{build(source)} {}

    [[nodiscard]] std::size_t size() const noexcept {
        return codes_.size();
    }

    // Precondition: row.value() < size(). Debug builds assert this contract.
    [[nodiscard]] bool is_null(RowId row) const noexcept {
        assert(row.value() < size());
        return !validity_.is_valid(row);
    }

    // The code at a null row is a zero placeholder and is not logically observable.
    [[nodiscard]] ValueId value_id(RowId row) const noexcept {
        assert(row.value() < size());
        return codes_.value(row);
    }

    // Preconditions: row.value() < size() and !is_null(row).
    [[nodiscard]] const T& value(RowId row) const noexcept {
        assert(row.value() < size());
        assert(!is_null(row));
        return dictionary_.value(codes_.value(row));
    }

    [[nodiscard]] const SortedDictionary<T>& dictionary() const noexcept {
        return dictionary_;
    }

    [[nodiscard]] const Codes& codes() const noexcept {
        return codes_;
    }

    [[nodiscard]] const ValidityBitmap& validity() const noexcept {
        return validity_;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return dictionary_.storage_bytes() + codes_.storage_bytes() +
               validity_.word_count() * sizeof(ValidityBitmap::Word);
    }

private:
    struct BuildResult {
        SortedDictionary<T> dictionary;
        std::vector<ValueId> codes;
        ValidityBitmap validity;
    };

    explicit BasicDictionaryColumn(BuildResult result)
        : dictionary_{std::move(result.dictionary)},
          codes_{make_codes(dictionary_.size(), result.codes)},
          validity_{std::move(result.validity)} {
        assert(codes_.size() == validity_.size());
    }

    [[nodiscard]] static BuildResult build(const FixedColumn<T>& source) {
        std::vector<T> non_null_values;
        non_null_values.reserve(source.size());
        ValidityBitmap validity;

        for (std::size_t index = 0; index < source.size(); ++index) {
            const RowId row{static_cast<std::uint64_t>(index)};
            if (source.is_null(row)) {
                validity.append_null();
            } else {
                const auto source_value = source.value(row);
                if (!SortedDictionary<T>::is_supported(source_value)) {
                    throw std::invalid_argument{"dictionary column contains unsupported float value"};
                }
                non_null_values.push_back(source_value);
                validity.append_valid();
            }
        }

        SortedDictionary<T> dictionary{non_null_values};
        std::vector<ValueId> codes;
        codes.reserve(source.size());
        for (std::size_t index = 0; index < source.size(); ++index) {
            const RowId row{static_cast<std::uint64_t>(index)};
            if (source.is_null(row)) {
                codes.push_back(0);
            } else {
                const auto id = dictionary.find(source.value(row));
                assert(id.has_value());
                codes.push_back(*id);
            }
        }
        return BuildResult{std::move(dictionary), std::move(codes), std::move(validity)};
    }

    [[nodiscard]] static Codes make_codes(std::size_t cardinality,
                                          std::vector<ValueId>& codes) {
        if constexpr (std::same_as<Codes, CodeVector>) {
            return CodeVector{std::move(codes)};
        } else {
            return PackedCodeVector{code_bit_width(cardinality), codes};
        }
    }

    // Invariant: codes_.size() == validity_.size(); every valid code is less
    // than dictionary_.size(); null rows contain the code-zero placeholder.
    SortedDictionary<T> dictionary_;
    Codes codes_;
    ValidityBitmap validity_;
};

template <FixedColumnValue T>
using DictionaryColumn = BasicDictionaryColumn<T, CodeVector>;

template <FixedColumnValue T>
using PackedDictionaryColumn = BasicDictionaryColumn<T, PackedCodeVector>;

}  // namespace susano
