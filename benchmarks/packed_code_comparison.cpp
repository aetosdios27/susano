#include "susano/code_vector.hpp"
#include "susano/packed_code_vector.hpp"
#include "susano/row_id.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

#if defined(__GNUC__) || defined(__clang__)
#define SUSANO_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define SUSANO_NOINLINE __declspec(noinline)
#else
#define SUSANO_NOINLINE
#endif

struct Config {
    std::size_t rows{1'000'000};
    std::uint8_t width{0};
    std::uint64_t seed{0x243f6a8885a308d3ULL};
};

struct Measurement {
    std::uint64_t checksum;
    std::uint64_t elapsed_ns;
};

[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
}

[[nodiscard]] std::uint64_t width_mask(std::uint8_t width) {
    return width == 32 ? 0xffff'ffffULL : (std::uint64_t{1} << width) - 1;
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

SUSANO_NOINLINE std::uint64_t scan_fixed(const susano::CodeVector& codes) {
    std::uint64_t sum = 0;
    for (const auto code : codes.values()) {
        sum += code;
    }
    return sum;
}

SUSANO_NOINLINE std::uint64_t scan_packed(const susano::PackedCodeVector& codes) {
    std::uint64_t sum = 0;
    for (std::size_t index = 0; index < codes.size(); ++index) {
        sum += codes.value(susano::RowId{static_cast<std::uint64_t>(index)});
    }
    return sum;
}

SUSANO_NOINLINE std::uint64_t random_fixed(const susano::CodeVector& codes,
                                           const std::vector<std::size_t>& rows) {
    std::uint64_t sum = 0;
    for (const auto index : rows) {
        sum += codes.value(susano::RowId{static_cast<std::uint64_t>(index)});
    }
    return sum;
}

SUSANO_NOINLINE std::uint64_t random_packed(const susano::PackedCodeVector& codes,
                                            const std::vector<std::size_t>& rows) {
    std::uint64_t sum = 0;
    for (const auto index : rows) {
        sum += codes.value(susano::RowId{static_cast<std::uint64_t>(index)});
    }
    return sum;
}

void emit(const Config& config,
          std::uint8_t width,
          std::string_view representation,
          std::size_t code_bytes,
          std::uint64_t build_ns,
          std::string_view operation,
          std::size_t values_processed,
          Measurement measurement) {
    const auto seconds = static_cast<double>(measurement.elapsed_ns) / 1'000'000'000.0;
    std::cout << config.seed << ',' << config.rows << ',' << static_cast<unsigned int>(width) << ','
              << representation << ',' << code_bytes << ','
              << static_cast<double>(code_bytes) / static_cast<double>(config.rows) << ','
              << build_ns << ',' << operation << ',' << values_processed << ','
              << measurement.elapsed_ns << ',' << static_cast<double>(values_processed) / seconds << ','
              << static_cast<double>(measurement.elapsed_ns) / static_cast<double>(values_processed)
              << ",nan," << measurement.checksum << '\n';
}

void run_width(const Config& config, std::uint8_t width) {
    const auto mask = width_mask(width);
    std::vector<susano::ValueId> expected;
    expected.reserve(config.rows);
    auto state = config.seed ^ static_cast<std::uint64_t>(width);
    for (std::size_t index = 0; index < config.rows; ++index) {
        expected.push_back(static_cast<susano::ValueId>((next_random(state) >> 16U) & mask));
    }
    expected.front() = 0;
    expected.back() = static_cast<susano::ValueId>(mask);

    const auto fixed_started = std::chrono::steady_clock::now();
    const susano::CodeVector fixed{expected};
    const auto fixed_finished = std::chrono::steady_clock::now();
    const auto packed_started = std::chrono::steady_clock::now();
    const susano::PackedCodeVector packed{width, expected};
    const auto packed_finished = std::chrono::steady_clock::now();
    const auto duration_ns = [](auto start, auto finish) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count());
    };

    std::vector<std::size_t> random_rows;
    random_rows.reserve(100'003);
    std::uint64_t random_state = config.seed ^ std::uint64_t{0x94d049bb133111ebULL};
    for (std::size_t sample = 0; sample < 100'003; ++sample) {
        random_rows.push_back(static_cast<std::size_t>((next_random(random_state) >> 16U) % config.rows));
    }

    const auto fixed_scan = measure([&] { return scan_fixed(fixed); });
    const auto packed_scan = measure([&] { return scan_packed(packed); });
    const auto fixed_random = measure([&] { return random_fixed(fixed, random_rows); });
    const auto packed_random = measure([&] { return random_packed(packed, random_rows); });
    if (fixed_scan.checksum != packed_scan.checksum || fixed_random.checksum != packed_random.checksum) {
        throw std::runtime_error{"packed width benchmark validation failed"};
    }

    const auto fixed_build = duration_ns(fixed_started, fixed_finished);
    const auto packed_build = duration_ns(packed_started, packed_finished);
    emit(config, width, "uint32", fixed.storage_bytes(), fixed_build, "sequential_scan",
         config.rows, fixed_scan);
    emit(config, width, "packed", packed.storage_bytes(), packed_build, "sequential_scan",
         config.rows, packed_scan);
    emit(config, width, "uint32", fixed.storage_bytes(), fixed_build, "random_lookup",
         random_rows.size(), fixed_random);
    emit(config, width, "packed", packed.storage_bytes(), packed_build, "random_lookup",
         random_rows.size(), packed_random);
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
        } else if (option == "--width" && parse_unsigned(argument, parsed) && parsed <= 32) {
            config.width = static_cast<std::uint8_t>(parsed);
        } else if (option == "--seed" && parse_unsigned(argument, parsed)) {
            config.seed = parsed;
        } else {
            return false;
        }
    }
    return config.rows > 0;
}

}  // namespace

int main(int argc, char** argv) {
    Config config;
    if (!parse_arguments(argc, argv, config)) {
        std::cerr << "usage: susano_bench_packed_codes [--rows N] [--width 1..32] [--seed N]\n";
        return 2;
    }

    std::cout << "seed,rows,bit_width,representation,code_bytes,bytes_per_value,build_ns,operation,"
                 "values_processed,elapsed_ns,rows_per_second,ns_per_value,cycles_per_value,checksum\n";
    try {
        if (config.width != 0) {
            run_width(config, config.width);
        } else {
            for (std::uint8_t width = 1; width <= 32; ++width) {
                run_width(config, width);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
