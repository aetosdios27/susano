#include "susano/generational_column.hpp"

#include <barrier>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <latch>
#include <thread>

namespace {

bool check_view(const susano::GenerationReadView<std::int32_t>& view,
                const std::initializer_list<std::int32_t>& expected) {
    if (view.size() != expected.size()) {
        return false;
    }
    std::size_t index = 0;
    for (const auto value : expected) {
        const susano::RowId row{static_cast<std::uint64_t>(index++)};
        if (view.is_null(row) || view.value(row) != value) {
            return false;
        }
    }
    return true;
}

bool check_forced_interleavings() {
    susano::FixedColumn<std::int32_t> initial;
    initial.append(10);
    initial.append(30);
    susano::GenerationalColumn<std::int32_t> column{initial};
    if (column.append(20) != susano::RowId{2}) {
        return false;
    }
    const auto reader_a = column.acquire_read_view();

    std::latch handoff_complete{1};
    std::latch allow_build{1};
    susano::MergeInterleavingHooks hooks;
    hooks.after_handoff = [&] {
        handoff_complete.count_down();
        allow_build.wait();
    };

    bool merged = false;
    std::thread merger{[&] { merged = column.merge_once(&hooks); }};
    handoff_complete.wait();

    const auto reader_b = column.acquire_read_view();
    const auto new_row = column.append(40);
    const auto reader_b_after_append = column.acquire_read_view();
    allow_build.count_down();
    merger.join();
    const auto reader_c = column.acquire_read_view();

    if (!merged || new_row != susano::RowId{3} || !check_view(reader_a, {10, 30, 20}) ||
        !check_view(reader_b, {10, 30, 20}) ||
        !check_view(reader_b_after_append, {10, 30, 20, 40}) ||
        !check_view(reader_c, {10, 30, 20, 40}) ||
        reader_c.generation() != susano::GenerationId{1}) {
        return false;
    }

    const auto& old_dictionary = reader_a.state().main->encoded().dictionary();
    const auto& new_dictionary = reader_c.state().main->encoded().dictionary();
    return old_dictionary.find(30) == susano::ValueId{1} &&
           new_dictionary.find(30) == susano::ValueId{2};
}

bool check_writer_handoff_races() {
    for (std::size_t repetition = 0; repetition < 1'000; ++repetition) {
        susano::FixedColumn<std::int32_t> empty;
        susano::GenerationalColumn<std::int32_t> column{empty};
        std::barrier start{2};
        susano::RowId appended{0};
        bool merged = false;
        std::thread writer{[&] {
            start.arrive_and_wait();
            appended = column.append(static_cast<std::int32_t>(repetition));
        }};
        std::thread merger{[&] {
            start.arrive_and_wait();
            merged = column.merge_once();
        }};
        writer.join();
        merger.join();
        const auto view = column.acquire_read_view();
        if (!merged || appended != susano::RowId{0} || view.size() != 1 ||
            view.is_null(susano::RowId{0}) ||
            view.value(susano::RowId{0}) != static_cast<std::int32_t>(repetition)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    if (!check_forced_interleavings() || !check_writer_handoff_races()) {
        std::cerr << "online merge interleaving or dictionary-domain behavior failed\n";
        return 1;
    }
    return 0;
}
