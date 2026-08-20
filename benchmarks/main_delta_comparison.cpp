#include "susano/main_delta_column.hpp"
#include "susano/main_delta_predicates.hpp"
#include "susano/physical_type.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

#if defined(__GNUC__) || defined(__clang__)
#define SUSANO_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define SUSANO_NOINLINE __declspec(noinline)
#else
#define SUSANO_NOINLINE
#endif

enum class Distribution {
    Uniform,
    Skewed,
};

enum class Operation {
    All,
    Point,
    Equality,
    Range,
    Scan,
};

struct Config {
    std::size_t main_rows{1'000'000};
    std::size_t cardinality{1'000};
    std::uint32_t delta_permille{100};
    std::uint32_t null_permille{0};
    Distribution distribution{Distribution::Uniform};
    Operation operation{Operation::All};
    std::string_view type{"int64"};
    std::uint64_t iterations{1};
    std::uint64_t seed{42};
};

struct Measurement {
    std::uint64_t checksum;
    std::uint64_t elapsed_ns;
};

struct AppendMeasurement {
    std::size_t rows;
    std::size_t bytes;
    std::uint64_t elapsed_ns;
};

[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
}

[[nodiscard]] std::size_t random_bounded(std::uint64_t& state, std::size_t bound) {
    return static_cast<std::size_t>((next_random(state) >> 16U) % bound);
}

[[nodiscard]] constexpr std::string_view distribution_name(Distribution distribution) {
    return distribution == Distribution::Uniform ? "uniform" : "skewed";
}

template <susano::FixedColumnValue T>
[[nodiscard]] T value_from_id(std::size_t id) {
    const auto physical = static_cast<std::uint64_t>(id) * 2ULL;
    if constexpr (std::same_as<T, double>) {
        return static_cast<double>(physical);
    }
    return static_cast<T>(physical);
}

template <susano::FixedColumnValue T>
[[nodiscard]] T next_value(std::uint64_t& state,
                           std::size_t cardinality,
                           Distribution distribution) {
    std::size_t id = 0;
    if (distribution == Distribution::Uniform) {
        id = random_bounded(state, cardinality);
    } else {
        id = std::min(random_bounded(state, cardinality),
                      random_bounded(state, cardinality));
    }
    return value_from_id<T>(id);
}

template <susano::FixedColumnValue T>
[[nodiscard]] susano::FixedColumn<T> generate_main(const Config& config) {
    susano::FixedColumn<T> source;
    auto state = config.seed;
    for (std::size_t row = 0; row < config.main_rows; ++row) {
        const auto value = row < config.cardinality
                               ? value_from_id<T>(row)
                               : next_value<T>(state, config.cardinality, config.distribution);
        const bool forced_valid = row < config.cardinality;
        const bool is_null = !forced_valid &&
                             random_bounded(state, 1'000) < config.null_permille;
        if (is_null) {
            source.append_null();
        } else {
            source.append(value);
        }
    }
    return source;
}

template <typename Function>
[[nodiscard]] Measurement measure(std::uint64_t iterations, Function&& function) {
    const auto warmup = function();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t checksum = 0;
    for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
        checksum += function() + iteration;
    }
    const auto finished = std::chrono::steady_clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    if (warmup == std::numeric_limits<std::uint64_t>::max()) {
        std::cerr << "unreachable warmup checksum\n";
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
    return Measurement{checksum, static_cast<std::uint64_t>(elapsed)};
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t point_checksum(
    const susano::MainDeltaColumn<T>& column,
    const std::vector<std::size_t>& rows) {
    if constexpr (std::same_as<T, double>) {
        double sum = 0.0;
        for (const auto index : rows) {
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            if (!column.is_null(row)) {
                sum += column.value(row);
            }
        }
        return std::bit_cast<std::uint64_t>(sum);
    } else {
        std::uint64_t sum = 0;
        for (const auto index : rows) {
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            if (!column.is_null(row)) {
                sum += static_cast<std::uint64_t>(column.value(row));
            }
        }
        return sum;
    }
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t equality_count(const susano::MainDeltaColumn<T>& column,
                                             T target) {
    return susano::main_delta_equal(column, target).size();
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t range_count(const susano::MainDeltaColumn<T>& column,
                                          T target) {
    return susano::main_delta_less(column, target).size();
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t sequential_checksum(const susano::MainDeltaColumn<T>& column) {
    if constexpr (std::same_as<T, double>) {
        double sum = 0.0;
        const auto& main = column.main().encoded();
        for (std::size_t index = 0; index < main.size(); ++index) {
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            if (!main.is_null(row)) {
                sum += main.value(row);
            }
        }
        for (const auto value : column.delta().raw().values()) {
            sum += value;
        }
        return std::bit_cast<std::uint64_t>(sum);
    } else {
        std::uint64_t sum = 0;
        const auto& main = column.main().encoded();
        for (std::size_t index = 0; index < main.size(); ++index) {
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            if (!main.is_null(row)) {
                sum += static_cast<std::uint64_t>(main.value(row));
            }
        }
        for (const auto value : column.delta().raw().values()) {
            sum += static_cast<std::uint64_t>(value);
        }
        return sum;
    }
}

[[nodiscard]] std::vector<std::size_t> point_rows(std::size_t logical_size,
                                                  std::uint64_t seed) {
    std::vector<std::size_t> rows;
    rows.reserve(100'003);
    auto state = seed;
    for (std::size_t sample = 0; sample < 100'003; ++sample) {
        rows.push_back(random_bounded(state, logical_size));
    }
    return rows;
}

template <susano::FixedColumnValue T>
[[nodiscard]] AppendMeasurement append_delta(susano::MainDeltaColumn<T>& column,
                                             std::size_t target_rows,
                                             const Config& config) {
    auto state = config.seed ^ std::uint64_t{0xd1b54a32d192ed03ULL};
    const auto before_bytes = column.delta().storage_bytes();
    const auto before_rows = column.delta_size();
    const auto started = std::chrono::steady_clock::now();
    while (column.delta_size() < target_rows) {
        const auto value = next_value<T>(state, config.cardinality, config.distribution);
        if (random_bounded(state, 1'000) < config.null_permille) {
            column.append_null();
        } else {
            column.append(value);
        }
    }
    const auto finished = std::chrono::steady_clock::now();
    return AppendMeasurement{
        column.delta_size() - before_rows,
        column.delta().storage_bytes() - before_bytes,
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count())};
}

template <susano::FixedColumnValue T>
constexpr std::string_view type_name() {
    if constexpr (std::same_as<T, std::int32_t>) {
        return susano::physical_type_name(susano::PhysicalType::Int32);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        return susano::physical_type_name(susano::PhysicalType::Int64);
    } else {
        return susano::physical_type_name(susano::PhysicalType::Float64);
    }
}

struct Layout {
    std::size_t dictionary_bytes;
    std::size_t code_bytes;
    std::size_t main_validity_bytes;
    std::size_t delta_value_bytes;
    std::size_t delta_validity_bytes;
    std::size_t total_bytes;
};

template <susano::FixedColumnValue T>
[[nodiscard]] Layout layout(const susano::MainDeltaColumn<T>& column) {
    const auto& main = column.main().encoded();
    const auto dictionary_bytes = main.dictionary().storage_bytes();
    const auto code_bytes = main.codes().storage_bytes();
    const auto main_validity_bytes = main.validity().word_count() * sizeof(susano::ValidityBitmap::Word);
    const auto delta_value_bytes = column.delta().raw().values().size_bytes();
    const auto delta_validity_bytes =
        column.delta().raw().validity().word_count() * sizeof(susano::ValidityBitmap::Word);
    return Layout{dictionary_bytes, code_bytes, main_validity_bytes, delta_value_bytes,
                  delta_validity_bytes, dictionary_bytes + code_bytes + main_validity_bytes +
                                            delta_value_bytes + delta_validity_bytes};
}

void emit_header() {
    std::cout << "seed,type,distribution,main_rows,delta_rows,delta_ratio,cardinality,main_build_ns,"
                 "main_dictionary_bytes,main_code_bytes,main_validity_bytes,delta_value_bytes,"
                 "delta_validity_bytes,total_bytes,bytes_per_value,append_rows,append_bytes,"
                 "append_elapsed_ns,append_ns_per_value,append_rows_per_second,operation,iterations,"
                 "values_processed,elapsed_ns,ns_per_value,rows_per_second,delta_penalty,checksum\n";
}

void emit(const Config& config,
          std::string_view type,
          std::size_t delta_rows,
          std::uint64_t main_build_ns,
          const Layout& memory,
          const AppendMeasurement& append,
          std::string_view operation,
          std::size_t values_processed,
          Measurement measurement,
          double penalty) {
    const auto logical_rows = config.main_rows + delta_rows;
    const auto append_ns = append.rows == 0 ? 0.0 :
        static_cast<double>(append.elapsed_ns) / static_cast<double>(append.rows);
    const auto append_rate = append.elapsed_ns == 0 ? 0.0 :
        static_cast<double>(append.rows) /
            (static_cast<double>(append.elapsed_ns) / 1'000'000'000.0);
    const auto seconds = static_cast<double>(measurement.elapsed_ns) / 1'000'000'000.0;

    std::cout << config.seed << ',' << type << ',' << distribution_name(config.distribution) << ','
              << config.main_rows << ',' << delta_rows << ','
              << static_cast<double>(delta_rows) / static_cast<double>(config.main_rows) << ','
              << config.cardinality << ',' << main_build_ns << ',' << memory.dictionary_bytes << ','
              << memory.code_bytes << ',' << memory.main_validity_bytes << ','
              << memory.delta_value_bytes << ',' << memory.delta_validity_bytes << ','
              << memory.total_bytes << ','
              << static_cast<double>(memory.total_bytes) / static_cast<double>(logical_rows) << ','
              << append.rows << ',' << append.bytes << ',' << append.elapsed_ns << ',' << append_ns
              << ',' << append_rate << ',' << operation << ',' << config.iterations << ','
              << values_processed << ',' << measurement.elapsed_ns << ','
              << static_cast<double>(measurement.elapsed_ns) / static_cast<double>(values_processed)
              << ',' << static_cast<double>(values_processed) / seconds << ',' << penalty << ','
              << measurement.checksum << '\n';
}

template <susano::FixedColumnValue T>
void run_ratios(const Config& config, std::span<const std::uint32_t> ratios) {
    const auto raw_main = generate_main<T>(config);
    const auto main_started = std::chrono::steady_clock::now();
    susano::MainColumn<T> main{raw_main};
    const auto main_finished = std::chrono::steady_clock::now();
    const auto main_build_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(main_finished - main_started).count());
    susano::MainDeltaColumn<T> column{std::move(main)};

    const auto equality_target = value_from_id<T>(config.cardinality / 2);
    const auto range_target = value_from_id<T>(config.cardinality / 2) + static_cast<T>(1);
    const auto baseline_iterations =
        config.operation == Operation::All ? config.iterations : std::uint64_t{1};
    const auto baseline_points = point_rows(column.size(), config.seed ^ 17ULL);
    const auto baseline_point = measure(baseline_iterations, [&] {
        return point_checksum(column, baseline_points);
    });
    const auto baseline_equal = measure(baseline_iterations, [&] {
        return equality_count(column, equality_target);
    });
    const auto baseline_range = measure(baseline_iterations, [&] {
        return range_count(column, range_target);
    });
    const auto baseline_scan = measure(baseline_iterations, [&] {
        return sequential_checksum(column);
    });

    for (const auto ratio : ratios) {
        const auto target_delta = config.main_rows * ratio / 1'000;
        const auto append = append_delta(column, target_delta, config);
        const auto memory = layout(column);
        const auto logical_values = column.size() * static_cast<std::size_t>(config.iterations);
        const auto points = point_rows(column.size(), config.seed ^ 17ULL);
        const auto relative_latency = [&](Measurement result, Measurement baseline) {
            const auto result_per_iteration =
                static_cast<double>(result.elapsed_ns) / static_cast<double>(config.iterations);
            const auto baseline_per_iteration =
                static_cast<double>(baseline.elapsed_ns) / static_cast<double>(baseline_iterations);
            return result_per_iteration / baseline_per_iteration;
        };


        const auto run_and_emit = [&](Operation operation) {
            if (operation == Operation::Point) {
                const auto result =
                    ratio == 0 && baseline_iterations == config.iterations
                        ? baseline_point
                        : measure(config.iterations, [&] {
                              return point_checksum(column, points);
                          });
                emit(config, type_name<T>(), column.delta_size(), main_build_ns, memory, append,
                     "point_mixed", points.size() * static_cast<std::size_t>(config.iterations),
                     result, relative_latency(result, baseline_point));
            } else if (operation == Operation::Equality) {
                const auto result =
                    ratio == 0 && baseline_iterations == config.iterations
                        ? baseline_equal
                        : measure(config.iterations, [&] {
                              return equality_count(column, equality_target);
                          });
                emit(config, type_name<T>(), column.delta_size(), main_build_ns, memory, append,
                     "equality", logical_values, result, relative_latency(result, baseline_equal));
            } else if (operation == Operation::Range) {
                const auto result =
                    ratio == 0 && baseline_iterations == config.iterations
                        ? baseline_range
                        : measure(config.iterations, [&] {
                              return range_count(column, range_target);
                          });
                emit(config, type_name<T>(), column.delta_size(), main_build_ns, memory, append,
                     "range_less", logical_values, result, relative_latency(result, baseline_range));
            } else if (operation == Operation::Scan) {
                const auto result =
                    ratio == 0 && baseline_iterations == config.iterations
                        ? baseline_scan
                        : measure(config.iterations, [&] {
                              return sequential_checksum(column);
                          });
                emit(config, type_name<T>(), column.delta_size(), main_build_ns, memory, append,
                     "sequential_scan", logical_values, result, relative_latency(result, baseline_scan));
            }
        };

        if (config.operation == Operation::All) {
            run_and_emit(Operation::Point);
            run_and_emit(Operation::Equality);
            run_and_emit(Operation::Range);
            run_and_emit(Operation::Scan);
        } else {
            run_and_emit(config.operation);
        }
    }
}

bool parse_unsigned(std::string_view input, std::uint64_t& value) {
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
    return result.ec == std::errc{} && result.ptr == input.data() + input.size();
}

bool parse_arguments(int argc, char** argv, Config& config) {
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option{argv[index]};
        const std::string_view argument{argv[index + 1]};
        std::uint64_t parsed = 0;
        if (option == "--main-rows" && parse_unsigned(argument, parsed)) {
            config.main_rows = static_cast<std::size_t>(parsed);
        } else if (option == "--cardinality" && parse_unsigned(argument, parsed)) {
            config.cardinality = static_cast<std::size_t>(parsed);
        } else if (option == "--delta-permille" && parse_unsigned(argument, parsed) && parsed <= 1'000) {
            config.delta_permille = static_cast<std::uint32_t>(parsed);
        } else if (option == "--null-permille" && parse_unsigned(argument, parsed) && parsed <= 1'000) {
            config.null_permille = static_cast<std::uint32_t>(parsed);
        } else if (option == "--iterations" && parse_unsigned(argument, parsed)) {
            config.iterations = parsed;
        } else if (option == "--seed" && parse_unsigned(argument, parsed)) {
            config.seed = parsed;
        } else if (option == "--distribution" && argument == "uniform") {
            config.distribution = Distribution::Uniform;
        } else if (option == "--distribution" && argument == "skewed") {
            config.distribution = Distribution::Skewed;
        } else if (option == "--type" &&
                   (argument == "int32" || argument == "int64" || argument == "float64")) {
            config.type = argument;
        } else if (option == "--operation" && argument == "all") {
            config.operation = Operation::All;
        } else if (option == "--operation" && argument == "point") {
            config.operation = Operation::Point;
        } else if (option == "--operation" && argument == "equality") {
            config.operation = Operation::Equality;
        } else if (option == "--operation" && argument == "range") {
            config.operation = Operation::Range;
        } else if (option == "--operation" && argument == "scan") {
            config.operation = Operation::Scan;
        } else {
            return false;
        }
    }
    return config.main_rows > 0 && config.cardinality > 0 &&
           config.cardinality <= config.main_rows && config.iterations > 0;
}

void run_config(const Config& config, std::span<const std::uint32_t> ratios) {
    if (config.type == "int32") {
        run_ratios<std::int32_t>(config, ratios);
    } else if (config.type == "int64") {
        run_ratios<std::int64_t>(config, ratios);
    } else {
        run_ratios<double>(config, ratios);
    }
}

void run_full_sweep() {
    constexpr std::array<std::uint32_t, 8> ratios{0, 1, 10, 50, 100, 250, 500, 1'000};
    constexpr std::array<Distribution, 2> distributions{
        Distribution::Uniform, Distribution::Skewed};
    constexpr std::array<std::string_view, 3> types{"int32", "int64", "float64"};
    for (const auto distribution : distributions) {
        for (const auto type : types) {
            Config config;
            config.distribution = distribution;
            config.type = type;
            config.delta_permille = 0;
            run_config(config, ratios);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    emit_header();
    if (argc == 3 && std::string_view{argv[1]} == "--sweep" &&
        std::string_view{argv[2]} == "all") {
        run_full_sweep();
        return 0;
    }

    Config config;
    if (!parse_arguments(argc, argv, config)) {
        std::cerr << "usage: susano_bench_main_delta [--sweep all] [--main-rows N] "
                     "[--cardinality C] [--delta-permille 0..1000] "
                     "[--null-permille 0..1000] [--distribution uniform|skewed] "
                     "[--type int32|int64|float64] "
                     "[--operation all|point|equality|range|scan] [--iterations N] [--seed N]\n";
        return 2;
    }

    const std::array<std::uint32_t, 1> ratio{config.delta_permille};
    run_config(config, ratio);
    return 0;
}
