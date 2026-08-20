#pragma once

#include "susano/concurrent_delta.hpp"
#include "susano/fixed_column.hpp"
#include "susano/generation_read_view.hpp"
#include "susano/main_column.hpp"
#include "susano/merge_lifecycle.hpp"
#include "susano/row_id.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
namespace susano {

struct MergeInterleavingHooks {
    std::function<void()> after_handoff;
    std::function<void()> before_publication;
    std::function<void()> after_publication;
};

struct MergeTimingSample {
    std::uint64_t handoff_ns;
    std::uint64_t build_ns;
    std::uint64_t publication_ns;
    std::uint64_t total_ns;
    std::size_t closed_delta_rows;
    std::size_t rebuilt_rows;
    std::size_t bytes_rebuilt;
    std::size_t builder_temporary_bytes;
    std::size_t estimated_peak_storage_bytes;
};

struct MergeTelemetrySnapshot {
    GenerationId current_generation;
    MergePhase phase;
    std::uint64_t handoff_count;
    std::uint64_t build_count;
    std::uint64_t publication_count;
    std::uint64_t reclamation_count;
    std::uint64_t rows_merged;
    std::uint64_t bytes_rebuilt;
    std::size_t closed_delta_rows;
    std::size_t active_delta_rows;
    std::size_t retired_generation_count;
    std::size_t estimated_retired_bytes;
    std::size_t peak_retired_generation_count;
    std::size_t peak_estimated_retired_bytes;
    std::uint64_t longest_reclamation_wait_ns;
    std::optional<GenerationId> oldest_retired_generation;
    std::vector<MergeTimingSample> timings;
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

    [[nodiscard]] MergeTelemetrySnapshot telemetry() const {
        const auto state = current_.load(std::memory_order_acquire);
        std::scoped_lock lock{telemetry_mutex_};
        cleanup_retired_locked();
        std::size_t retired_bytes = 0;
        std::optional<GenerationId> oldest;
        for (const auto& retired : retired_) {
            if (!retired.state.expired()) {
                retired_bytes += retired.estimated_bytes;
                if (!oldest || retired.generation < *oldest) {
                    oldest = retired.generation;
                }
            }
        }
        return MergeTelemetrySnapshot{
            state->generation,
            lifecycle_.phase(),
            handoff_count_,
            build_count_,
            publication_count_,
            reclamation_count_,
            rows_merged_,
            bytes_rebuilt_,
            state->closed_delta ? state->closed_delta->size() : 0,
            state->active_delta->visible_size(),
            retired_.size(),
            retired_bytes,
            peak_retired_count_,
            peak_retired_bytes_,
            longest_reclamation_wait_ns_,
            oldest,
            timings_,
        };
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

        const auto total_started = std::chrono::steady_clock::now();
        const auto handoff_started = total_started;
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
            auto retired = current_.exchange(std::move(handoff_state), std::memory_order_acq_rel);
            retire_state(std::move(retired));
        }
        const auto handoff_finished = std::chrono::steady_clock::now();

        static_cast<void>(lifecycle_.transition(MergePhase::Handoff, MergePhase::Building));
        if (hooks && hooks->after_handoff) {
            hooks->after_handoff();
        }

        const auto build_started = std::chrono::steady_clock::now();
        auto build = build_main(*input_state->main, *closed_delta);
        const auto build_finished = std::chrono::steady_clock::now();
        static_cast<void>(lifecycle_.transition(MergePhase::Building, MergePhase::Publishing));
        if (hooks && hooks->before_publication) {
            hooks->before_publication();
        }

        const auto publication_started = std::chrono::steady_clock::now();
        const auto next_generation = GenerationId{input_state->generation.value() + 1};
        auto published_state = std::make_shared<const GenerationState<T>>(
            GenerationState<T>{next_generation, build.main, nullptr, fresh_active, fresh_base});
        auto retired = current_.exchange(std::move(published_state), std::memory_order_acq_rel);
        retire_state(std::move(retired));
        const auto publication_finished = std::chrono::steady_clock::now();
        if (hooks && hooks->after_publication) {
            hooks->after_publication();
        }

        static_cast<void>(lifecycle_.transition(MergePhase::Publishing, MergePhase::Retired));
        static_cast<void>(lifecycle_.transition(MergePhase::Retired, MergePhase::Stable));
        const auto total_finished = std::chrono::steady_clock::now();

        const auto duration_ns = [](auto start, auto finish) {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count());
        };
        const auto estimated_peak = input_state->main->storage_bytes() +
                                    closed_delta->storage_bytes() +
                                    fresh_active->allocated_bytes() +
                                    build.main->storage_bytes() +
                                    build.temporary_bytes;
        {
            std::scoped_lock lock{telemetry_mutex_};
            ++handoff_count_;
            ++build_count_;
            ++publication_count_;
            rows_merged_ += closed_delta->size();
            bytes_rebuilt_ += build.main->storage_bytes();
            timings_.push_back(MergeTimingSample{
                duration_ns(handoff_started, handoff_finished),
                duration_ns(build_started, build_finished),
                duration_ns(publication_started, publication_finished),
                duration_ns(total_started, total_finished),
                closed_delta->size(),
                build.main->size(),
                build.main->storage_bytes(),
                build.temporary_bytes,
                estimated_peak,
            });
        }
        return true;
    }

private:
    struct BuildResult {
        std::shared_ptr<const MainColumn<T>> main;
        std::size_t temporary_bytes;
    };

    struct RetiredState {
        GenerationId generation;
        std::weak_ptr<const GenerationState<T>> state;
        std::size_t estimated_bytes;
        std::chrono::steady_clock::time_point retired_at;
    };

    [[nodiscard]] static BuildResult build_main(
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
        const auto temporary_bytes = raw.values().size_bytes() +
                                     raw.validity().word_count() * sizeof(ValidityBitmap::Word);
        return BuildResult{std::make_shared<const MainColumn<T>>(raw), temporary_bytes};
    }

    void retire_state(std::shared_ptr<const GenerationState<T>> state) {
        std::scoped_lock lock{telemetry_mutex_};
        cleanup_retired_locked();
        const auto bytes = state->estimated_storage_bytes();
        retired_.push_back(
            RetiredState{state->generation, state, bytes, std::chrono::steady_clock::now()});
        peak_retired_count_ = std::max(peak_retired_count_, retired_.size());
        std::size_t current_bytes = 0;
        for (const auto& retired : retired_) {
            current_bytes += retired.estimated_bytes;
        }
        peak_retired_bytes_ = std::max(peak_retired_bytes_, current_bytes);
    }

    void cleanup_retired_locked() const {
        const auto now = std::chrono::steady_clock::now();
        auto iterator = retired_.begin();
        while (iterator != retired_.end()) {
            if (iterator->state.expired()) {
                const auto wait = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now - iterator->retired_at)
                        .count());
                longest_reclamation_wait_ns_ = std::max(longest_reclamation_wait_ns_, wait);
                ++reclamation_count_;
                iterator = retired_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    mutable std::mutex handoff_mutex_;
    std::mutex merge_mutex_;
    MergeLifecycle lifecycle_;
    std::atomic<std::shared_ptr<const GenerationState<T>>> current_;

    mutable std::mutex telemetry_mutex_;
    mutable std::vector<RetiredState> retired_;
    mutable std::uint64_t reclamation_count_{0};
    mutable std::uint64_t longest_reclamation_wait_ns_{0};
    std::uint64_t handoff_count_{0};
    std::uint64_t build_count_{0};
    std::uint64_t publication_count_{0};
    std::uint64_t rows_merged_{0};
    std::uint64_t bytes_rebuilt_{0};
    std::size_t peak_retired_count_{0};
    std::size_t peak_retired_bytes_{0};
    std::vector<MergeTimingSample> timings_;
};

}  // namespace susano
