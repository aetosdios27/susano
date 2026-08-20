#include "susano/dictionary_column.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

template <typename T>
bool exactly_equal(T left, T right) {
    if constexpr (std::same_as<T, double>) {
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    }
    return left == right;
}

template <susano::FixedColumnValue T>
bool check_encoded_column(const susano::FixedColumn<T>& source, std::string_view label) {
    const susano::DictionaryColumn<T> fixed{source};
    const susano::PackedDictionaryColumn<T> packed{source};
    const susano::DictionaryColumn<T> repeated{source};

    if (fixed.size() != source.size() || packed.size() != source.size() ||
        fixed.validity().size() != source.size() || packed.validity().size() != source.size() ||
        fixed.codes().size() != source.size() || packed.codes().size() != source.size() ||
        fixed.dictionary().size() != packed.dictionary().size() ||
        fixed.dictionary().size() != repeated.dictionary().size()) {
        std::cerr << label << " encoded length invariant failed\n";
        return false;
    }

    for (std::size_t id_index = 0; id_index < fixed.dictionary().size(); ++id_index) {
        const auto id = static_cast<susano::ValueId>(id_index);
        if (!exactly_equal(fixed.dictionary().value(id), packed.dictionary().value(id)) ||
            !exactly_equal(fixed.dictionary().value(id), repeated.dictionary().value(id))) {
            std::cerr << label << " deterministic dictionary mismatch\n";
            return false;
        }
    }

    for (std::size_t index = 0; index < source.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (fixed.is_null(row) != source.is_null(row) || packed.is_null(row) != source.is_null(row) ||
            fixed.value_id(row) != packed.value_id(row) ||
            fixed.value_id(row) != repeated.value_id(row)) {
            std::cerr << label << " nullness or deterministic code mismatch at row " << index << '\n';
            return false;
        }
        if (source.is_null(row)) {
            if (fixed.value_id(row) != 0) {
                std::cerr << label << " null code placeholder is not zero\n";
                return false;
            }
            continue;
        }
        if (fixed.value_id(row) >= fixed.dictionary().size() ||
            !exactly_equal(fixed.value(row), source.value(row)) ||
            !exactly_equal(packed.value(row), source.value(row))) {
            std::cerr << label << " decoded value mismatch at row " << index << '\n';
            return false;
        }
    }
    return true;
}

template <susano::FixedColumnValue T>
T make_value(std::uint64_t value) {
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(static_cast<std::int64_t>(value % 2001ULL) - 1000) / 8.0;
    } else {
        return static_cast<T>(static_cast<std::int64_t>(value % 2001ULL) - 1000);
    }
}

template <susano::FixedColumnValue T>
bool check_cases(std::string_view label, std::uint64_t seed) {
    susano::FixedColumn<T> empty;
    if (!check_encoded_column(empty, label)) {
        return false;
    }

    susano::FixedColumn<T> one;
    one.append(make_value<T>(7));
    if (!check_encoded_column(one, label)) {
        return false;
    }

    susano::FixedColumn<T> all_equal;
    for (std::size_t index = 0; index < 257; ++index) {
        all_equal.append(make_value<T>(42));
    }
    if (!check_encoded_column(all_equal, label)) {
        return false;
    }

    susano::FixedColumn<T> all_null;
    for (std::size_t index = 0; index < 129; ++index) {
        all_null.append_null();
    }
    if (!check_encoded_column(all_null, label)) {
        return false;
    }

    susano::FixedColumn<T> sorted_unique;
    for (std::size_t index = 0; index < 513; ++index) {
        sorted_unique.append(make_value<T>(index));
    }
    if (!check_encoded_column(sorted_unique, label)) {
        return false;
    }

    susano::FixedColumn<T> reverse_unique;
    for (std::size_t index = 513; index > 0; --index) {
        reverse_unique.append(make_value<T>(index - 1));
    }
    if (!check_encoded_column(reverse_unique, label)) {
        return false;
    }

    constexpr std::size_t generated_rows = 100'003;
    susano::FixedColumn<T> generated;
    auto state = seed;
    for (std::size_t index = 0; index < generated_rows; ++index) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const bool is_null = index == 0 || index == generated_rows - 1 || index % 2 == 0 ||
                             state % 11ULL == 0;
        if (is_null) {
            generated.append_null();
        } else {
            generated.append(make_value<T>(state));
        }
    }
    return check_encoded_column(generated, label);
}

}  // namespace

int main() {
    if (!check_cases<std::int32_t>("int32", 0x123456789abcdef0ULL) ||
        !check_cases<std::int64_t>("int64", 0x0fedcba987654321ULL) ||
        !check_cases<double>("float64", 0x55aa55aa55aa55aaULL)) {
        return 1;
    }
    return 0;
}
