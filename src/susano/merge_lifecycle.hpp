#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace susano {

enum class MergePhase : std::uint8_t {
    Stable,
    Handoff,
    Building,
    Publishing,
    Retired,
};

[[nodiscard]] constexpr bool valid_merge_transition(MergePhase from, MergePhase to) noexcept {
    switch (from) {
        case MergePhase::Stable:
            return to == MergePhase::Handoff;
        case MergePhase::Handoff:
            return to == MergePhase::Building;
        case MergePhase::Building:
            return to == MergePhase::Publishing;
        case MergePhase::Publishing:
            return to == MergePhase::Retired;
        case MergePhase::Retired:
            return to == MergePhase::Stable;
    }
    return false;
}

[[nodiscard]] constexpr std::string_view merge_phase_name(MergePhase phase) noexcept {
    using namespace std::literals;
    switch (phase) {
        case MergePhase::Stable:
            return "stable"sv;
        case MergePhase::Handoff:
            return "handoff"sv;
        case MergePhase::Building:
            return "building"sv;
        case MergePhase::Publishing:
            return "publishing"sv;
        case MergePhase::Retired:
            return "retired"sv;
    }
    return "unknown"sv;
}

class MergeLifecycle {
public:
    [[nodiscard]] MergePhase phase() const noexcept {
        return phase_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool transition(MergePhase expected, MergePhase next) noexcept {
        if (!valid_merge_transition(expected, next)) {
            return false;
        }
        return phase_.compare_exchange_strong(expected, next,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire);
    }

private:
    std::atomic<MergePhase> phase_{MergePhase::Stable};
};

}  // namespace susano
