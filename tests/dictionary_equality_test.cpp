#include "susano/dictionary_predicates.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

template <susano::FixedColumnValue T>
std::vector<susano::RowId> reference_equal(const susano::FixedColumn<T>& source, T target) {
    std::vector<susano::RowId> matches;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (!source.is_null(row) && source.value(row) == target) {
            matches.push_back(row);
        }
    }
    return matches;
}

template <susano::FixedColumnValue T>
bool check_targets(const susano::FixedColumn<T>& source,
                   const std::vector<T>& targets,
                   std::string_view label) {
    const susano::DictionaryColumn<T> fixed{source};
    const susano::PackedDictionaryColumn<T> packed{source};
    for (const auto target : targets) {
        const auto expected = reference_equal(source, target);
        if (susano::dictionary_equal(fixed, target) != expected ||
            susano::dictionary_equal(packed, target) != expected) {
            std::cerr << label << " encoded equality differs from decoded reference\n";
            return false;
        }
    }
    return true;
}

template <susano::FixedColumnValue T>
T value(std::int64_t input) {
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(input) / 4.0;
    }
    return static_cast<T>(input);
}

template <susano::FixedColumnValue T>
bool check_equality_cases(std::string_view label) {
    susano::FixedColumn<T> mixed;
    for (std::int64_t index = -10; index <= 10; ++index) {
        mixed.append(value<T>(index));
        if (index % 3 == 0) {
            mixed.append(value<T>(index));
        }
        if (index % 4 == 0) {
            mixed.append_null();
        }
    }
    const std::vector<T> mixed_targets{value<T>(-10), value<T>(0), value<T>(10), value<T>(11)};
    if (!check_targets(mixed, mixed_targets, label)) {
        return false;
    }

    susano::FixedColumn<T> all_match;
    for (std::size_t index = 0; index < 257; ++index) {
        all_match.append(value<T>(5));
    }
    const std::vector<T> cardinality_one_targets{value<T>(5), value<T>(6)};
    if (!check_targets(all_match, cardinality_one_targets, label)) {
        return false;
    }

    susano::FixedColumn<T> one_match;
    one_match.append_null();
    one_match.append(value<T>(1));
    one_match.append(value<T>(2));
    one_match.append_null();
    one_match.append(value<T>(3));
    const std::vector<T> one_match_targets{value<T>(2), value<T>(4)};
    return check_targets(one_match, one_match_targets, label);
}

}  // namespace

int main() {
    if (!check_equality_cases<std::int32_t>("int32") ||
        !check_equality_cases<std::int64_t>("int64") ||
        !check_equality_cases<double>("float64")) {
        return 1;
    }
    return 0;
}
