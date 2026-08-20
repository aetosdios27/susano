#include "susano/sorted_dictionary.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

template <susano::FixedColumnValue T>
bool check_basic_dictionary(std::span<const T> input, std::span<const T> expected) {
    const susano::SortedDictionary<T> dictionary{input};
    if (dictionary.size() != expected.size() || dictionary.values().size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto id = static_cast<susano::ValueId>(index);
        if (dictionary.value(id) != expected[index] || dictionary.find(expected[index]) != id) {
            return false;
        }
    }
    return true;
}

bool check_determinism() {
    constexpr std::array<std::int64_t, 12> input{42, -7, 42, 10, -7, 0, 99, 10, 1, 99, 42, 1};
    const susano::SortedDictionary<std::int64_t> first{input};

    for (int repetition = 0; repetition < 20; ++repetition) {
        std::vector<std::uint64_t> allocator_noise(static_cast<std::size_t>(repetition + 1) * 31, 17);
        const susano::SortedDictionary<std::int64_t> next{input};
        if (first.values().size() != next.values().size()) {
            return false;
        }
        for (std::size_t index = 0; index < first.size(); ++index) {
            const auto id = static_cast<susano::ValueId>(index);
            if (first.value(id) != next.value(id) || first.find(first.value(id)) != next.find(next.value(id))) {
                return false;
            }
        }
        if (allocator_noise.empty()) {
            return false;
        }
    }
    return true;
}

bool check_float_policy() {
    constexpr std::array<double, 7> input{-3.5, -0.0, 2.25, 0.0, -3.5, 9.0, 2.25};
    const susano::SortedDictionary<double> dictionary{input};
    constexpr std::array<double, 4> expected{-3.5, 0.0, 2.25, 9.0};
    if (!check_basic_dictionary<double>(input, expected)) {
        return false;
    }

    const auto zero_id = dictionary.find(-0.0);
    if (!zero_id || std::bit_cast<std::uint64_t>(dictionary.value(*zero_id)) !=
                        std::bit_cast<std::uint64_t>(0.0)) {
        return false;
    }
    if (dictionary.find(std::numeric_limits<double>::infinity()).has_value() ||
        dictionary.find(std::numeric_limits<double>::quiet_NaN()).has_value()) {
        return false;
    }

    for (const auto unsupported : {std::numeric_limits<double>::infinity(),
                                   -std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::quiet_NaN()}) {
        try {
            const std::array<double, 1> invalid{unsupported};
            const susano::SortedDictionary<double> rejected{invalid};
            static_cast<void>(rejected);
            return false;
        } catch (const std::invalid_argument&) {
        }
    }
    return true;
}

}  // namespace

int main() {
    constexpr std::array<std::int32_t, 7> int32_input{42, 7, 42, 10, 7, -5, 10};
    constexpr std::array<std::int32_t, 4> int32_expected{-5, 7, 10, 42};
    constexpr std::array<std::int64_t, 6> int64_input{9, 9, -4, 12, -4, 0};
    constexpr std::array<std::int64_t, 4> int64_expected{-4, 0, 9, 12};
    const std::array<std::int32_t, 0> empty{};

    if (!check_basic_dictionary<std::int32_t>(int32_input, int32_expected) ||
        !check_basic_dictionary<std::int64_t>(int64_input, int64_expected) ||
        !check_basic_dictionary<std::int32_t>(empty, empty) ||
        !check_determinism() || !check_float_policy()) {
        std::cerr << "sorted dictionary construction, lookup, or ordering failed\n";
        return 1;
    }

    const susano::SortedDictionary<std::int32_t> dictionary{int32_input};
    if (dictionary.find(8).has_value() || dictionary.storage_bytes() != 4 * sizeof(std::int32_t)) {
        std::cerr << "sorted dictionary absence or storage accounting failed\n";
        return 1;
    }
    return 0;
}
