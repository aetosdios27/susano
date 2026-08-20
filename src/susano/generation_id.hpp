#pragma once

#include <compare>
#include <cstdint>

namespace susano {

class GenerationId {
public:
    explicit constexpr GenerationId(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    auto operator<=>(const GenerationId&) const = default;

private:
    std::uint64_t value_;
};

}  // namespace susano
