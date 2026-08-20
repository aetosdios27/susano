#include "susano/dictionary_column.hpp"
#include "susano/fixed_column.hpp"
#include "susano/physical_type.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
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

enum class Sortedness {
    Random,
    Sorted,
};

struct Config {
    std::size_t rows{100'000};
    std::size_t cardinality{1'000};
    std::uint32_t null_permille{50};
    Distribution distribution{Distribution::Uniform};
    Sortedness sortedness{Sortedness::Random};
    std::string_view type{"int64"};
    std::uint64_t seed{0x9e3779b97f4a7c15ULL};
};

struct Measurement {
    std::uint64_t checksum;
    std::uint64_t elapsed_ns;
};

template <susano::FixedColumnValue T>
struct GeneratedData {
    std::vector<T> values;
    std::vector<bool> nulls;
    std::size_t valid_count{0};
};

[[nodiscard]] constexpr std::string_view distribution_name(Distribution distribution) {
    return distribution == Distribution::Uniform ? "uniform" : "skewed";
}

[[nodiscard]] constexpr std::string_view sortedness_name(Sortedness sortedness) {
    return sortedness == Sortedness::Random ? "random" : "sorted";
}

[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
}

[[nodiscard]] std::size_t random_bounded(std::uint64_t& state, std::size_t bound) {
    return static_cast<std::size_t>((next_random(state) >> 16U) % bound);
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
[[nodiscard]] GeneratedData<T> generate_data(const Config& config) {
    GeneratedData<T> data;
    data.values.resize(config.rows);
    data.nulls.resize(config.rows);
    auto state = config.seed;

    for (std::size_t row = 0; row < config.rows; ++row) {
        std::size_t id = 0;
        if (row < config.cardinality) {
            id = row;
        } else if (config.distribution == Distribution::Uniform) {
            id = random_bounded(state, config.cardinality);
        } else {
            const auto first = random_bounded(state, config.cardinality);
            const auto second = random_bounded(state, config.cardinality);
            id = std::min(first, second);
        }

        const bool forced_valid = row < config.cardinality;
        const bool is_null = !forced_valid && random_bounded(state, 1'000) < config.null_permille;
        data.values[row] = value_from_id<T>(id);
        data.nulls[row] = is_null;
        data.valid_count += is_null ? 0 : 1;
    }

    if (config.sortedness == Sortedness::Sorted) {
        std::vector<T> valid_values;
        valid_values.reserve(data.valid_count);
        for (std::size_t row = 0; row < config.rows; ++row) {
            if (!data.nulls[row]) {
                valid_values.push_back(data.values[row]);
            }
        }
        std::sort(valid_values.begin(), valid_values.end());
        std::size_t valid_index = 0;
        for (std::size_t row = 0; row < config.rows; ++row) {
            if (!data.nulls[row]) {
                data.values[row] = valid_values[valid_index++];
            }
        }
    }
    return data;
}

template <susano::FixedColumnValue T>
[[nodiscard]] susano::FixedColumn<T> build_raw(const GeneratedData<T>& data) {
    susano::FixedColumn<T> column;
    for (std::size_t row = 0; row < data.values.size(); ++row) {
        if (data.nulls[row]) {
            column.append_null();
        } else {
            column.append(data.values[row]);
        }
    }
    return column;
}

template <typename Function>
[[nodiscard]] Measurement measure(Function&& function) {
    const auto warmup = function();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto started = std::chrono::steady_clock::now();
    const auto checksum = function();
    const auto finished = std::chrono::steady_clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    if (warmup == std::numeric_limits<std::uint64_t>::max()) {
        std::cerr << "unreachable warmup checksum\n";
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();
    return Measurement{checksum, static_cast<std::uint64_t>(elapsed)};
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t scan_raw(const susano::FixedColumn<T>& column) {
    if constexpr (std::same_as<T, double>) {
        double sum = 0.0;
        for (const auto value : column.values()) {
            sum += value;
        }
        return std::bit_cast<std::uint64_t>(sum);
    } else {
        std::uint64_t sum = 0;
        for (const auto value : column.values()) {
            sum += static_cast<std::uint64_t>(value);
        }
        return sum;
    }
}

template <susano::FixedColumnValue T, susano::DictionaryCodeStorage Codes>
SUSANO_NOINLINE std::uint64_t scan_decoded(
    const susano::BasicDictionaryColumn<T, Codes>& column) {
    if constexpr (std::same_as<T, double>) {
        double sum = 0.0;
        for (std::size_t index = 0; index < column.size(); ++index) {
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            if (!column.is_null(row)) {
                sum += column.value(row);
            }
        }
        return std::bit_cast<std::uint64_t>(sum);
    } else {
        std::uint64_t sum = 0;
        for (std::size_t index = 0; index < column.size(); ++index) {
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            if (!column.is_null(row)) {
                sum += static_cast<std::uint64_t>(column.value(row));
            }
        }
        return sum;
    }
}

template <susano::FixedColumnValue T, susano::DictionaryCodeStorage Codes>
SUSANO_NOINLINE std::uint64_t scan_codes(
    const susano::BasicDictionaryColumn<T, Codes>& column) {
    std::uint64_t sum = 0;
    for (std::size_t index = 0; index < column.size(); ++index) {
        sum += column.value_id(susano::RowId{static_cast<std::uint64_t>(index)});
    }
    return sum;
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t raw_equal_count(const susano::FixedColumn<T>& column, T target) {
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < column.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        count += !column.is_null(row) && column.value(row) == target ? 1ULL : 0ULL;
    }
    return count;
}

template <susano::FixedColumnValue T, susano::DictionaryCodeStorage Codes>
SUSANO_NOINLINE std::uint64_t encoded_equal_count(
    const susano::BasicDictionaryColumn<T, Codes>& column,
    T target) {
    const auto target_id = column.dictionary().find(target);
    if (!target_id) {
        return 0;
    }
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < column.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        count += !column.is_null(row) && column.value_id(row) == *target_id ? 1ULL : 0ULL;
    }
    return count;
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t raw_range_count(const susano::FixedColumn<T>& column, T target) {
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < column.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        count += !column.is_null(row) && column.value(row) < target ? 1ULL : 0ULL;
    }
    return count;
}

template <susano::FixedColumnValue T, susano::DictionaryCodeStorage Codes>
SUSANO_NOINLINE std::uint64_t encoded_range_count(
    const susano::BasicDictionaryColumn<T, Codes>& column,
    T target) {
    const auto boundary = column.dictionary().lower_bound_id(target);
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < column.size(); ++index) {
        const susano::RowId row{static_cast<std::uint64_t>(index)};
        count += !column.is_null(row) && column.value_id(row) < boundary ? 1ULL : 0ULL;
    }
    return count;
}

template <susano::FixedColumnValue T>
SUSANO_NOINLINE std::uint64_t random_raw(const susano::FixedColumn<T>& column,
                                         const std::vector<std::size_t>& rows) {
    if constexpr (std::same_as<T, double>) {
        double sum = 0.0;
        for (const auto index : rows) {
            sum += column.value(susano::RowId{static_cast<std::uint64_t>(index)});
        }
        return std::bit_cast<std::uint64_t>(sum);
    } else {
        std::uint64_t sum = 0;
        for (const auto index : rows) {
            sum += static_cast<std::uint64_t>(column.value(
                susano::RowId{static_cast<std::uint64_t>(index)}));
        }
        return sum;
    }
}

template <susano::FixedColumnValue T, susano::DictionaryCodeStorage Codes>
SUSANO_NOINLINE std::uint64_t random_encoded(
    const susano::BasicDictionaryColumn<T, Codes>& column,
    const std::vector<std::size_t>& rows) {
    if constexpr (std::same_as<T, double>) {
        double sum = 0.0;
        for (const auto index : rows) {
            const susano::RowId row{static_cast<std::uint64_t>(index)};
            sum += column.is_null(row) ? 0.0 : column.value(row);
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

struct Layout {
    std::string_view representation;
    std::size_t dictionary_bytes;
    std::size_t code_bytes;
    std::size_t validity_bytes;
    std::size_t total_bytes;
    std::uint64_t build_ns;
};

void emit(const Config& config,
          std::string_view type,
          std::size_t actual_cardinality,
          std::size_t valid_count,
          std::size_t raw_bytes,
          const Layout& layout,
          std::string_view operation,
          std::size_t values_processed,
          Measurement measurement,
          double selectivity) {
    const auto null_fraction = 1.0 - static_cast<double>(valid_count) / static_cast<double>(config.rows);
    const auto requested_null_fraction = static_cast<double>(config.null_permille) / 1'000.0;
    const auto ratio = static_cast<double>(actual_cardinality) / static_cast<double>(config.rows);
    const auto bytes_per_value = static_cast<double>(layout.total_bytes) / static_cast<double>(config.rows);
    const auto compression_ratio = static_cast<double>(raw_bytes) / static_cast<double>(layout.total_bytes);
    const auto seconds = static_cast<double>(measurement.elapsed_ns) / 1'000'000'000.0;
    const auto throughput = static_cast<double>(values_processed) / seconds;
    const auto ns_per_value = static_cast<double>(measurement.elapsed_ns) /
                              static_cast<double>(values_processed);

    std::cout << config.seed << ',' << type << ',' << config.rows << ',' << config.cardinality << ','
              << actual_cardinality << ',' << ratio << ',' << requested_null_fraction << ','
              << null_fraction << ',' << distribution_name(config.distribution) << ','
              << sortedness_name(config.sortedness) << ',' << layout.representation << ','
              << raw_bytes << ',' << layout.dictionary_bytes << ',' << layout.code_bytes << ','
              << layout.validity_bytes << ',' << layout.total_bytes << ',' << bytes_per_value << ','
              << compression_ratio << ',' << layout.build_ns << ',' << operation << ','
              << values_processed << ',' << measurement.elapsed_ns << ',' << throughput << ','
              << ns_per_value << ",nan," << selectivity << ',' << measurement.checksum << '\n';
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

template <susano::FixedColumnValue T>
void run_case(const Config& config) {
    const auto generated = generate_data<T>(config);

    const auto raw_started = std::chrono::steady_clock::now();
    const auto raw = build_raw(generated);
    const auto raw_finished = std::chrono::steady_clock::now();
    const auto fixed_started = std::chrono::steady_clock::now();
    const susano::DictionaryColumn<T> fixed{raw};
    const auto fixed_finished = std::chrono::steady_clock::now();
    const auto packed_started = std::chrono::steady_clock::now();
    const susano::PackedDictionaryColumn<T> packed{raw};
    const auto packed_finished = std::chrono::steady_clock::now();

    const auto duration_ns = [](auto start, auto finish) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count());
    };
    const auto validity_bytes = raw.validity().word_count() * sizeof(susano::ValidityBitmap::Word);
    const auto raw_bytes = raw.values().size_bytes() + validity_bytes;
    const Layout raw_layout{"raw", 0, raw.values().size_bytes(), validity_bytes, raw_bytes,
                            duration_ns(raw_started, raw_finished)};
    const Layout fixed_layout{"dictionary_u32", fixed.dictionary().storage_bytes(),
                              fixed.codes().storage_bytes(), validity_bytes, fixed.storage_bytes(),
                              duration_ns(fixed_started, fixed_finished)};
    const Layout packed_layout{"dictionary_packed", packed.dictionary().storage_bytes(),
                               packed.codes().storage_bytes(), validity_bytes, packed.storage_bytes(),
                               duration_ns(packed_started, packed_finished)};

    const auto actual_cardinality = fixed.dictionary().size();
    const auto equality_target = value_from_id<T>(config.cardinality / 2);
    const auto range_target = value_from_id<T>(config.cardinality / 2) + static_cast<T>(1);
    std::vector<std::size_t> random_rows;
    random_rows.reserve(100'003);
    std::uint64_t random_state = config.seed ^ std::uint64_t{0xd1b54a32d192ed03ULL};
    for (std::size_t sample = 0; sample < 100'003; ++sample) {
        random_rows.push_back(random_bounded(random_state, config.rows));
    }

    const auto raw_scan = measure([&] { return scan_raw(raw); });
    const auto fixed_decode = measure([&] { return scan_decoded(fixed); });
    const auto packed_decode = measure([&] { return scan_decoded(packed); });
    const auto fixed_codes = measure([&] { return scan_codes(fixed); });
    const auto packed_codes = measure([&] { return scan_codes(packed); });
    const auto raw_equal = measure([&] { return raw_equal_count(raw, equality_target); });
    const auto fixed_equal = measure([&] { return encoded_equal_count(fixed, equality_target); });
    const auto packed_equal = measure([&] { return encoded_equal_count(packed, equality_target); });
    const auto raw_range = measure([&] { return raw_range_count(raw, range_target); });
    const auto fixed_range = measure([&] { return encoded_range_count(fixed, range_target); });
    const auto packed_range = measure([&] { return encoded_range_count(packed, range_target); });
    const auto raw_random = measure([&] { return random_raw(raw, random_rows); });
    const auto fixed_random = measure([&] { return random_encoded(fixed, random_rows); });
    const auto packed_random = measure([&] { return random_encoded(packed, random_rows); });

    if (raw_scan.checksum != fixed_decode.checksum || raw_scan.checksum != packed_decode.checksum ||
        fixed_codes.checksum != packed_codes.checksum || raw_equal.checksum != fixed_equal.checksum ||
        raw_equal.checksum != packed_equal.checksum || raw_range.checksum != fixed_range.checksum ||
        raw_range.checksum != packed_range.checksum || raw_random.checksum != fixed_random.checksum ||
        raw_random.checksum != packed_random.checksum) {
        throw std::runtime_error{"benchmark representation validation failed"};
    }

    const auto type = type_name<T>();
    const auto equality_selectivity = static_cast<double>(raw_equal.checksum) /
                                      static_cast<double>(generated.valid_count);
    const auto range_selectivity = static_cast<double>(raw_range.checksum) /
                                   static_cast<double>(generated.valid_count);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, raw_layout,
         "decoded_scan", config.rows, raw_scan, 0.0);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, fixed_layout,
         "decoded_scan", config.rows, fixed_decode, 0.0);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, packed_layout,
         "decoded_scan", config.rows, packed_decode, 0.0);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, fixed_layout,
         "encoded_scan", config.rows, fixed_codes, 0.0);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, packed_layout,
         "encoded_scan", config.rows, packed_codes, 0.0);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, raw_layout,
         "equality_decoded", config.rows, raw_equal, equality_selectivity);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, fixed_layout,
         "equality_encoded", config.rows, fixed_equal, equality_selectivity);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, packed_layout,
         "equality_encoded", config.rows, packed_equal, equality_selectivity);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, raw_layout,
         "range_decoded", config.rows, raw_range, range_selectivity);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, fixed_layout,
         "range_encoded", config.rows, fixed_range, range_selectivity);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, packed_layout,
         "range_encoded", config.rows, packed_range, range_selectivity);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, raw_layout,
         "random_lookup", random_rows.size(), raw_random, 0.0);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, fixed_layout,
         "random_lookup", random_rows.size(), fixed_random, 0.0);
    emit(config, type, actual_cardinality, generated.valid_count, raw_bytes, packed_layout,
         "random_lookup", random_rows.size(), packed_random, 0.0);
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
        if (option == "--rows" && parse_unsigned(argument, parsed)) {
            config.rows = static_cast<std::size_t>(parsed);
        } else if (option == "--cardinality" && parse_unsigned(argument, parsed)) {
            config.cardinality = static_cast<std::size_t>(parsed);
        } else if (option == "--null-permille" && parse_unsigned(argument, parsed) && parsed <= 1'000) {
            config.null_permille = static_cast<std::uint32_t>(parsed);
        } else if (option == "--seed" && parse_unsigned(argument, parsed)) {
            config.seed = parsed;
        } else if (option == "--distribution" && argument == "uniform") {
            config.distribution = Distribution::Uniform;
        } else if (option == "--distribution" && argument == "skewed") {
            config.distribution = Distribution::Skewed;
        } else if (option == "--sortedness" && argument == "random") {
            config.sortedness = Sortedness::Random;
        } else if (option == "--sortedness" && argument == "sorted") {
            config.sortedness = Sortedness::Sorted;
        } else if (option == "--type" &&
                   (argument == "int32" || argument == "int64" || argument == "float64")) {
            config.type = argument;
        } else {
            return false;
        }
    }
    return config.rows > 0 && config.cardinality > 0 && config.cardinality <= config.rows &&
           config.cardinality <= std::numeric_limits<susano::ValueId>::max();
}

void print_header() {
    std::cout << "seed,type,rows,requested_cardinality,actual_cardinality,cardinality_ratio,"
                 "requested_null_fraction,null_fraction,distribution,sortedness,representation,"
                 "raw_bytes,dictionary_bytes,code_bytes,validity_bytes,total_bytes,bytes_per_value,"
                 "compression_ratio,build_ns,operation,values_processed,elapsed_ns,rows_per_second,"
                 "ns_per_value,cycles_per_value,predicate_selectivity,checksum\n";
}

}  // namespace

int main(int argc, char** argv) {
    Config config;
    if (!parse_arguments(argc, argv, config)) {
        std::cerr << "usage: susano_bench_dictionary [--rows N] [--cardinality C] "
                     "[--null-permille 0..1000] [--distribution uniform|skewed] "
                     "[--sortedness random|sorted] [--type int32|int64|float64] [--seed N]\n";
        return 2;
    }

    print_header();
    try {
        if (config.type == "int32") {
            run_case<std::int32_t>(config);
        } else if (config.type == "int64") {
            run_case<std::int64_t>(config);
        } else {
            run_case<double>(config);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
