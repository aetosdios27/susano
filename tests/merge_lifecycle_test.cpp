#include "susano/merge_lifecycle.hpp"

#include <array>
#include <iostream>
#include <string_view>
#include <utility>

int main() {
    using susano::MergePhase;
    constexpr std::array<std::pair<MergePhase, MergePhase>, 5> legal{{
        {MergePhase::Stable, MergePhase::Handoff},
        {MergePhase::Handoff, MergePhase::Building},
        {MergePhase::Building, MergePhase::Publishing},
        {MergePhase::Publishing, MergePhase::Retired},
        {MergePhase::Retired, MergePhase::Stable},
    }};
    for (const auto& [from, to] : legal) {
        if (!susano::valid_merge_transition(from, to)) {
            std::cerr << "legal merge transition rejected\n";
            return 1;
        }
    }

    susano::MergeLifecycle lifecycle;
    if (lifecycle.phase() != MergePhase::Stable ||
        lifecycle.transition(MergePhase::Stable, MergePhase::Building) ||
        !lifecycle.transition(MergePhase::Stable, MergePhase::Handoff) ||
        lifecycle.transition(MergePhase::Stable, MergePhase::Handoff) ||
        !lifecycle.transition(MergePhase::Handoff, MergePhase::Building) ||
        !lifecycle.transition(MergePhase::Building, MergePhase::Publishing) ||
        !lifecycle.transition(MergePhase::Publishing, MergePhase::Retired) ||
        !lifecycle.transition(MergePhase::Retired, MergePhase::Stable) ||
        lifecycle.phase() != MergePhase::Stable ||
        susano::merge_phase_name(MergePhase::Publishing) != std::string_view{"publishing"}) {
        std::cerr << "merge lifecycle transition semantics failed\n";
        return 1;
    }
    return 0;
}
