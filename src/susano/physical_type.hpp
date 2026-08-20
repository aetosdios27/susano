#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace susano {

enum class PhysicalType : std::uint8_t {
    Int32 = 0,
    Int64 = 1,
    Float64 = 2,
};

[[nodiscard]] constexpr std::size_t physical_type_width(PhysicalType type) noexcept {
    switch (type) {
        case PhysicalType::Int32:
            return sizeof(std::int32_t);
        case PhysicalType::Int64:
            return sizeof(std::int64_t);
        case PhysicalType::Float64:
            return sizeof(double);
    }
    std::unreachable();
}

[[nodiscard]] constexpr std::string_view physical_type_name(PhysicalType type) noexcept {
    using namespace std::literals;

    switch (type) {
        case PhysicalType::Int32:
            return "int32"sv;
        case PhysicalType::Int64:
            return "int64"sv;
        case PhysicalType::Float64:
            return "float64"sv;
    }
    std::unreachable();
}

}  // namespace susano
