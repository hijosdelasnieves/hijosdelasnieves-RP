#pragma once
#include <cstddef>
#include <cstdint>

namespace hdn::studio {
// RGBA/BGRA8 over a known black surface, BEFORE CEF draws. Alpha alone is not
// geometry. Stop early; this is a first-frame liveness probe, not a screenshot
// or a claim that the avatar's face, lighting or clothing are correct.
inline bool hasVisiblePixels(const std::uint8_t* bytes, std::size_t stride,
                             std::size_t width, std::size_t height)
{
  if (!bytes || !width || !height || width > stride / 4)
    return false;
  unsigned hits = 0;
  for (std::size_t y = 0; y < height; ++y) {
    const auto* row = bytes + y * stride;
    for (std::size_t x = 0; x < width; ++x) {
      const auto* pixel = row + x * 4;
      if ((pixel[0] > 3 || pixel[1] > 3 || pixel[2] > 3) && ++hits >= 16)
        return true;
    }
  }
  return false;
}
}
