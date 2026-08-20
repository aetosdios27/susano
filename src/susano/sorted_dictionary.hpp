#pragma once

#include "susano/fixed_column.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace susano {

using ValueId = std::uint32_t;

template <FixedColumnValue T>
class SortedDictionary {
public:
    using value_type = T;
    using value_id_type = ValueId;

    SortedDictionary() = default;

    explicit SortedDictionary(std::span<const T> input) {
        values_.reserve(input.size());
        for (const auto value : input) {
            if (!is_supported(value)) {
                throw std::invalid_argument{"dictionary float values must be finite"};
            }
            values_.push_back(canonicalize(value));
        }

        std::sort(values_.begin(), values_.end());
        values_.erase(std::unique(values_.begin(), values_.end()), values_.end());
        if (values_.size() > std::numeric_limits<ValueId>::max()) {
            throw std::length_error{"dictionary cardinality exceeds ValueId capacity"};
        }
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return values_.empty();
    }

    // Precondition: id < size(). Debug builds assert this contract.
    [[nodiscard]] const T& value(ValueId id) const noexcept {
        assert(id < values_.size());
        return values_[id];
    }

    [[nodiscard]] std::optional<ValueId> find(T value_to_find) const noexcept {
        if (!is_supported(value_to_find)) {
            return std::nullopt;
        }
        const auto normalized = canonicalize(value_to_find);
        const auto position = std::lower_bound(values_.begin(), values_.end(), normalized);
        if (position == values_.end() || *position != normalized) {
            return std::nullopt;
        }
        return static_cast<ValueId>(position - values_.begin());
    }

    [[nodiscard]] ValueId lower_bound_id(T target) const {
        if (!is_supported(target)) {
            throw std::invalid_argument{"dictionary range target must be finite"};
        }
        const auto normalized = canonicalize(target);
        const auto position = std::lower_bound(values_.begin(), values_.end(), normalized);
        return static_cast<ValueId>(position - values_.begin());
    }

    [[nodiscard]] ValueId upper_bound_id(T target) const {
        if (!is_supported(target)) {
            throw std::invalid_argument{"dictionary range target must be finite"};
        }
        const auto normalized = canonicalize(target);
        const auto position = std::upper_bound(values_.begin(), values_.end(), normalized);
        return static_cast<ValueId>(position - values_.begin());
    }

    [[nodiscard]] std::span<const T> values() const noexcept {
        return values_;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept {
        return values_.size() * sizeof(T);
    }

    [[nodiscard]] static bool is_supported(T value) noexcept {
        if constexpr (std::same_as<T, double>) {
            return std::isfinite(value);
        }
        return true;
    }

private:
    [[nodiscard]] static T canonicalize(T value) noexcept {
        if constexpr (std::same_as<T, double>) {
            return value == 0.0 ? 0.0 : value;
        }
        return value;
    }

    std::vector<T> values_;
};

}  // namespace susano
