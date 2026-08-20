#include "susano/dictionary_predicates.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

enum class Relation {
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

template <susano::FixedColumnValue T>
bool compare(T value, T target, Relation relation) {
    switch (relation) {
        case Relation::Less:
            return value < target;
        case Relation::LessEqual:
            return value <= target;
        case Relation::Greater:
            return value > target;
        case Relation::GreaterEqual:
            return value >= target;
    }
    return false;
}

template <susano::FixedColumnValue T>
std::vector<susano::RowId> reference_range(const susano::FixedColumn<T>& source,
                                           T target,
                                           Relation relation) {
    std::vector<susano::RowId> matches;
    for (std::size_t index = 0; index < source.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (!source.is_null(row) && compare(source.value(row), target, relation)) {
            matches.push_back(row);
        }
    }
    return matches;
}

template <susano::FixedColumnValue T, susano::DictionaryCodeStorage Codes>
std::vector<susano::RowId> encoded_range(const susano::BasicDictionaryColumn<T, Codes>& column,
                                         T target,
                                         Relation relation) {
    switch (relation) {
        case Relation::Less:
            return susano::dictionary_less(column, target);
        case Relation::LessEqual:
            return susano::dictionary_less_equal(column, target);
        case Relation::Greater:
            return susano::dictionary_greater(column, target);
        case Relation::GreaterEqual:
            return susano::dictionary_greater_equal(column, target);
    }
    return {};
}

template <susano::FixedColumnValue T>
T value(std::int64_t input) {
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(input) / 2.0;
    }
    return static_cast<T>(input);
}

template <susano::FixedColumnValue T>
bool check_ranges(std::string_view label) {
    susano::FixedColumn<T> source;
    for (const auto input : {10, 20, 30, 40, 20, 10, 40, 30}) {
        source.append(value<T>(input));
        if (input == 20 || input == 40) {
            source.append_null();
        }
    }

    const susano::DictionaryColumn<T> fixed{source};
    const susano::PackedDictionaryColumn<T> packed{source};
    constexpr std::array<std::int64_t, 7> target_inputs{5, 10, 15, 20, 30, 40, 45};
    constexpr std::array<Relation, 4> relations{
        Relation::Less, Relation::LessEqual, Relation::Greater, Relation::GreaterEqual};

    for (const auto target_input : target_inputs) {
        const auto target = value<T>(target_input);
        for (const auto relation : relations) {
            const auto expected = reference_range(source, target, relation);
            if (encoded_range(fixed, target, relation) != expected ||
                encoded_range(packed, target, relation) != expected) {
                std::cerr << label << " encoded range differs from decoded reference\n";
                return false;
            }
        }
    }
    return true;
}

bool check_non_finite_rejection() {
    susano::FixedColumn<double> source;
    source.append(1.0);
    const susano::DictionaryColumn<double> column{source};
    try {
        const auto matches = susano::dictionary_less(column, std::numeric_limits<double>::infinity());
        static_cast<void>(matches);
        return false;
    } catch (const std::invalid_argument&) {
    }
    return true;
}

}  // namespace

int main() {
    if (!check_ranges<std::int32_t>("int32") || !check_ranges<std::int64_t>("int64") ||
        !check_ranges<double>("float64") || !check_non_finite_rejection()) {
        return 1;
    }
    return 0;
}
