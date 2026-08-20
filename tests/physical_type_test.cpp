#include "susano/physical_type.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

constexpr bool physical_types_are_distinct() {
    using susano::PhysicalType;
    return PhysicalType::Int32 != PhysicalType::Int64 &&
           PhysicalType::Int32 != PhysicalType::Float64 &&
           PhysicalType::Int64 != PhysicalType::Float64;
}

static_assert(physical_types_are_distinct());
static_assert(susano::physical_type_width(susano::PhysicalType::Int32) == sizeof(std::int32_t));
static_assert(susano::physical_type_width(susano::PhysicalType::Int64) == sizeof(std::int64_t));
static_assert(susano::physical_type_width(susano::PhysicalType::Float64) == sizeof(double));
static_assert(susano::physical_type_name(susano::PhysicalType::Int32) == std::string_view{"int32"});
static_assert(susano::physical_type_name(susano::PhysicalType::Int64) == std::string_view{"int64"});
static_assert(susano::physical_type_name(susano::PhysicalType::Float64) == std::string_view{"float64"});

}  // namespace

int main() {
    if (!physical_types_are_distinct()) {
        std::cerr << "physical types must have distinct identifiers\n";
        return 1;
    }

    return 0;
}
