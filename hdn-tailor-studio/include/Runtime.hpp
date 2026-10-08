#pragma once
#include <array>
#include <cstdint>

namespace hdn::studio {
constexpr bool supportedRuntime(std::array<std::uint16_t, 4> version)
{
  return version == std::array<std::uint16_t, 4>{ 1, 5, 97, 0 } ||
    version == std::array<std::uint16_t, 4>{ 1, 6, 1170, 0 };
}
}
