#pragma once

#include "susano/concurrent_delta.hpp"
#include "susano/dictionary_predicates.hpp"
#include "susano/generation_id.hpp"
#include "susano/main_column.hpp"
#include "susano/row_id.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace susano {

template <FixedColumnValue T>
struct GenerationState {
    GenerationId generation;
    std::shared_ptr<const MainColumn<T>> main;
    std::shared_ptr<const ClosedDeltaColumn<T>> closed_delta;
    std::shared_ptr<ConcurrentDeltaColumn<T>> active_delta;
    std::uint64_t active_delta_base;

    [[nodiscard]] std::size_t estimated_storage_bytes() const {
        return main->storage_bytes() +
               (closed_delta ? closed_delta->storage_bytes() : 0) +
               active_delta->allocated_bytes();
    }
};

template <FixedColumnValue T>
class GenerationReadView {
public:
    explicit GenerationReadView(std::shared_ptr<const GenerationState<T>> state)
        : state_{std::move(state)},
          active_prefix_{state_->active_delta->snapshot(state_->active_delta_base)},
          logical_size_{static_cast<std::size_t>(state_->active_delta_base) + active_prefix_.size()} {}

    [[nodiscard]] GenerationId generation() const noexcept {
        return state_->generation;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return logical_size_;
    }

    [[nodiscard]] std::size_t main_size() const noexcept {
        return state_->main->size();
    }

    [[nodiscard]] std::size_t closed_delta_size() const noexcept {
        return state_->closed_delta ? state_->closed_delta->size() : 0;
    }

    [[nodiscard]] std::size_t active_delta_size() const noexcept {
        return active_prefix_.size();
    }

    [[nodiscard]] bool is_null(RowId row) const noexcept {
        assert(row.value() < logical_size_);
        if (row.value() < state_->main->size()) {
            return state_->main->is_null(row);
        }
        if (row.value() < state_->active_delta_base) {
            assert(state_->closed_delta);
            return state_->closed_delta->is_null(
                static_cast<std::size_t>(row.value() - state_->main->size()));
        }
        return active_prefix_.is_null(
            static_cast<std::size_t>(row.value() - state_->active_delta_base));
    }

    [[nodiscard]] T value(RowId row) const noexcept {
        assert(row.value() < logical_size_);
        if (row.value() < state_->main->size()) {
            return state_->main->value(row);
        }
        if (row.value() < state_->active_delta_base) {
            assert(state_->closed_delta);
            return state_->closed_delta->value(
                static_cast<std::size_t>(row.value() - state_->main->size()));
        }
        return active_prefix_.value(
            static_cast<std::size_t>(row.value() - state_->active_delta_base));
    }

    [[nodiscard]] const GenerationState<T>& state() const noexcept {
        return *state_;
    }

    [[nodiscard]] const DeltaReadPrefix<T>& active_prefix() const noexcept {
        return active_prefix_;
    }

private:
    std::shared_ptr<const GenerationState<T>> state_;
    DeltaReadPrefix<T> active_prefix_;
    std::size_t logical_size_;
};

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> generation_equal(const GenerationReadView<T>& view, T target) {
    auto matches = dictionary_equal(view.state().main->encoded(), target);
    if (view.state().closed_delta) {
        for (std::size_t offset = 0; offset < view.state().closed_delta->size(); ++offset) {
            if (!view.state().closed_delta->is_null(offset) &&
                view.state().closed_delta->value(offset) == target) {
                matches.emplace_back(view.state().closed_delta->logical_base() + offset);
            }
        }
    }
    for (std::size_t offset = 0; offset < view.active_prefix().size(); ++offset) {
        if (!view.active_prefix().is_null(offset) && view.active_prefix().value(offset) == target) {
            matches.emplace_back(view.active_prefix().logical_base() + offset);
        }
    }
    return matches;
}

namespace detail {

template <FixedColumnValue T, typename MainPredicate, typename RawPredicate>
[[nodiscard]] std::vector<RowId> generation_range(
    const GenerationReadView<T>& view,
    T target,
    MainPredicate main_predicate,
    RawPredicate raw_predicate) {
    auto matches = main_predicate(view.state().main->encoded(), target);
    if (view.state().closed_delta) {
        for (std::size_t offset = 0; offset < view.state().closed_delta->size(); ++offset) {
            if (!view.state().closed_delta->is_null(offset) &&
                raw_predicate(view.state().closed_delta->value(offset), target)) {
                matches.emplace_back(view.state().closed_delta->logical_base() + offset);
            }
        }
    }
    for (std::size_t offset = 0; offset < view.active_prefix().size(); ++offset) {
        if (!view.active_prefix().is_null(offset) &&
            raw_predicate(view.active_prefix().value(offset), target)) {
            matches.emplace_back(view.active_prefix().logical_base() + offset);
        }
    }
    return matches;
}

}  // namespace detail

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> generation_less(const GenerationReadView<T>& view, T target) {
    return detail::generation_range(
        view, target,
        [](const auto& main, T value) { return dictionary_less(main, value); },
        [](T value, T boundary) { return value < boundary; });
}

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> generation_less_equal(const GenerationReadView<T>& view,
                                                       T target) {
    return detail::generation_range(
        view, target,
        [](const auto& main, T value) { return dictionary_less_equal(main, value); },
        [](T value, T boundary) { return value <= boundary; });
}

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> generation_greater(const GenerationReadView<T>& view, T target) {
    return detail::generation_range(
        view, target,
        [](const auto& main, T value) { return dictionary_greater(main, value); },
        [](T value, T boundary) { return value > boundary; });
}

template <FixedColumnValue T>
[[nodiscard]] std::vector<RowId> generation_greater_equal(const GenerationReadView<T>& view,
                                                          T target) {
    return detail::generation_range(
        view, target,
        [](const auto& main, T value) { return dictionary_greater_equal(main, value); },
        [](T value, T boundary) { return value >= boundary; });
}

}  // namespace susano
