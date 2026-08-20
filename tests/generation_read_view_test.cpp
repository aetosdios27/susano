#include "susano/generation_read_view.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace {

bool check_reference(const susano::GenerationReadView<std::int32_t>& view,
                     const std::vector<std::optional<std::int32_t>>& reference) {
    if (view.size() != reference.size()) {
        return false;
    }
    for (std::size_t index = 0; index < reference.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        if (view.is_null(row) != !reference[index].has_value() ||
            (reference[index] && view.value(row) != *reference[index])) {
            return false;
        }
    }
    return true;
}

std::vector<susano::RowId> reference_equal(
    const std::vector<std::optional<std::int32_t>>& reference,
    std::int32_t target) {
    std::vector<susano::RowId> matches;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        if (reference[index] && *reference[index] == target) {
            matches.emplace_back(static_cast<std::uint64_t>(index));
        }
    }
    return matches;
}

}  // namespace

int main() {
    susano::FixedColumn<std::int32_t> raw_main;
    raw_main.append(10);
    raw_main.append(30);
    auto main = std::make_shared<const susano::MainColumn<std::int32_t>>(raw_main);
    auto old_active = std::make_shared<susano::ConcurrentDeltaColumn<std::int32_t>>();
    static_cast<void>(old_active->append(20));
    static_cast<void>(old_active->append_null());

    auto stable_state = std::make_shared<const susano::GenerationState<std::int32_t>>(
        susano::GenerationState<std::int32_t>{susano::GenerationId{0}, main, nullptr,
                                               old_active, 2});
    const susano::GenerationReadView<std::int32_t> reader_a{stable_state};
    static_cast<void>(old_active->append(99));
    if (!check_reference(reader_a, {10, 30, 20, std::nullopt})) {
        std::cerr << "pre-handoff reader changed after append\n";
        return 1;
    }

    const auto closed = old_active->freeze(2);
    auto fresh_active = std::make_shared<susano::ConcurrentDeltaColumn<std::int32_t>>();
    static_cast<void>(fresh_active->append(40));
    auto building_state = std::make_shared<const susano::GenerationState<std::int32_t>>(
        susano::GenerationState<std::int32_t>{susano::GenerationId{0}, main, closed,
                                               fresh_active, 5});
    const susano::GenerationReadView<std::int32_t> reader_b{building_state};
    static_cast<void>(fresh_active->append(50));
    const std::vector<std::optional<std::int32_t>> before_publication{
        10, 30, 20, std::nullopt, 99, 40};
    if (!check_reference(reader_b, before_publication) ||
        susano::generation_equal(reader_b, 40) != reference_equal(before_publication, 40) ||
        susano::generation_less(reader_b, 30).size() != 2) {
        std::cerr << "building reader state is inconsistent\n";
        return 1;
    }

    susano::FixedColumn<std::int32_t> rebuilt_raw;
    rebuilt_raw.append(10);
    rebuilt_raw.append(30);
    rebuilt_raw.append(20);
    rebuilt_raw.append_null();
    rebuilt_raw.append(99);
    auto rebuilt_main = std::make_shared<const susano::MainColumn<std::int32_t>>(rebuilt_raw);
    auto published_state = std::make_shared<const susano::GenerationState<std::int32_t>>(
        susano::GenerationState<std::int32_t>{susano::GenerationId{1}, rebuilt_main, nullptr,
                                               fresh_active, 5});
    const susano::GenerationReadView<std::int32_t> reader_c{published_state};
    const std::vector<std::optional<std::int32_t>> after_publication{
        10, 30, 20, std::nullopt, 99, 40, 50};

    if (reader_a.generation() != susano::GenerationId{0} || reader_b.size() != 6 ||
        reader_c.generation() != susano::GenerationId{1} ||
        !check_reference(reader_b, before_publication) ||
        !check_reference(reader_c, after_publication) ||
        susano::generation_equal(reader_c, 50) != reference_equal(after_publication, 50) ||
        susano::generation_greater_equal(reader_c, 40).size() != 3) {
        std::cerr << "generation read-view timing semantics failed\n";
        return 1;
    }
    return 0;
}
