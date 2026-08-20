#pragma once

#include "susano/concurrent_delta.hpp"
#include "susano/fixed_column.hpp"
#include "susano/generation_read_view.hpp"
#include "susano/main_column.hpp"
#include "susano/merge_lifecycle.hpp"
#include "susano/row_id.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace susano {

struct MergeInterleavingHooks {
    std::function<void()> after_handoff;
    std::function<void()> before_publication;
    std::function<void()> after_publication;
};

template <FixedColumnValue T>
class GenerationalColumn {
public:
    explicit GenerationalColumn(const FixedColumn<T>& initial) {
        auto main = std::make_shared<const MainColumn<T>>(initial);
        auto active = std::make_shared<ConcurrentDeltaColumn<T>>();
        auto state = std::make_shared<const GenerationState<T>>(
            GenerationState<T>{GenerationId{0}, std::move(main), nullptr,
                               std::move(active), initial.size()});
        current_.store(std::move(state), std::memory_order_release);
    }

    // Single-writer operation. Returning publishes exactly one stable logical RowId.
    [[nodiscard]] RowId append(T value) {
        std::scoped_lock lock{handoff_mutex_};
        const auto state = current_.load(std::memory_order_acquire);
        const auto offset = state->active_delta->append(value);
        return RowId{state->active_delta_base + offset};
    }

    [[nodiscard]] RowId append_null() {
        std::scoped_lock lock{handoff_mutex_};
        const auto state = current_.load(std::memory_order_acquire);
        const auto offset = state->active_delta->append_null();
        return RowId{state->active_delta_base + offset};
    }

    // The acquire load plus active-prefix acquire is the read-view linearization point.
    [[nodiscard]] GenerationReadView<T> acquire_read_view() const {
        return GenerationReadView<T>{current_.load(std::memory_order_acquire)};
    }

    [[nodiscard]] GenerationId current_generation() const noexcept {
        return current_.load(std::memory_order_acquire)->generation;
    }

    [[nodiscard]] MergePhase merge_phase() const noexcept {
        return lifecycle_.phase();
    }

    // Explicit mechanism only. No merge policy is embedded here.
    [[nodiscard]] bool merge_once(const MergeInterleavingHooks* hooks = nullptr) {
        std::unique_lock merge_lock{merge_mutex_, std::try_to_lock};
        if (!merge_lock.owns_lock()) {
            return false;
        }
        if (!lifecycle_.transition(MergePhase::Stable, MergePhase::Handoff)) {
            return false;
        }

        std::shared_ptr<const GenerationState<T>> input_state;
        std::shared_ptr<const ClosedDeltaColumn<T>> closed_delta;
        std::shared_ptr<ConcurrentDeltaColumn<T>> fresh_active;
        std::uint64_t fresh_base = 0;
        {
            // This is the only writer-sensitive handoff critical section.
            std::scoped_lock handoff_lock{handoff_mutex_};
            input_state = current_.load(std::memory_order_acquire);
            closed_delta = input_state->active_delta->freeze(input_state->active_delta_base);
            fresh_base = input_state->active_delta_base + closed_delta->size();
            fresh_active = std::make_shared<ConcurrentDeltaColumn<T>>();
            auto handoff_state = std::make_shared<const GenerationState<T>>(
                GenerationState<T>{input_state->generation, input_state->main, closed_delta,
                                   fresh_active, fresh_base});
            current_.store(std::move(handoff_state), std::memory_order_release);
        }

        static_cast<void>(lifecycle_.transition(MergePhase::Handoff, MergePhase::Building));
        if (hooks && hooks->after_handoff) {
            hooks->after_handoff();
        }

        auto rebuilt_main = build_main(*input_state->main, *closed_delta);
        static_cast<void>(lifecycle_.transition(MergePhase::Building, MergePhase::Publishing));
        if (hooks && hooks->before_publication) {
            hooks->before_publication();
        }

        const auto next_generation = GenerationId{input_state->generation.value() + 1};
        auto published_state = std::make_shared<const GenerationState<T>>(
            GenerationState<T>{next_generation, std::move(rebuilt_main), nullptr,
                               fresh_active, fresh_base});
        current_.store(std::move(published_state), std::memory_order_release);
        if (hooks && hooks->after_publication) {
            hooks->after_publication();
        }

        static_cast<void>(lifecycle_.transition(MergePhase::Publishing, MergePhase::Retired));
        static_cast<void>(lifecycle_.transition(MergePhase::Retired, MergePhase::Stable));
        return true;
    }

private:
    [[nodiscard]] static std::shared_ptr<const MainColumn<T>> build_main(
        const MainColumn<T>& main,
        const ClosedDeltaColumn<T>& closed_delta) {
        FixedColumn<T> raw;
        for (std::size_t index = 0; index < main.size(); ++index) {
            const RowId row{static_cast<std::uint64_t>(index)};
            if (main.is_null(row)) {
                raw.append_null();
            } else {
                raw.append(main.value(row));
            }
        }
        for (std::size_t offset = 0; offset < closed_delta.size(); ++offset) {
            if (closed_delta.is_null(offset)) {
                raw.append_null();
            } else {
                raw.append(closed_delta.value(offset));
            }
        }
        return std::make_shared<const MainColumn<T>>(raw);
    }

    mutable std::mutex handoff_mutex_;
    std::mutex merge_mutex_;
    MergeLifecycle lifecycle_;
    std::atomic<std::shared_ptr<const GenerationState<T>>> current_;
};

}  // namespace susano
