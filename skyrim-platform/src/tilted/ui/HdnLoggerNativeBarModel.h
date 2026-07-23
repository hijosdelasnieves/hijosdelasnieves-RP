#pragma once

#include <cmath>

namespace CEFUtils {
inline double HdnLoggerPingPong(double phase) noexcept
{
  if (!std::isfinite(phase)) {
    return 0.0;
  }

  double wrapped = std::fmod(phase, 2.0);
  if (wrapped < 0.0) {
    wrapped += 2.0;
  }
  return wrapped <= 1.0 ? wrapped : 2.0 - wrapped;
}

inline double HdnLoggerBarValue(double startPhase, double elapsedMs,
                                double travelMs) noexcept
{
  if (!std::isfinite(elapsedMs) || elapsedMs < 0.0 ||
      !std::isfinite(travelMs) || travelMs <= 0.0) {
    return HdnLoggerPingPong(startPhase);
  }
  return HdnLoggerPingPong(startPhase + elapsedMs / travelMs);
}

inline bool IsHdnLoggerBarGeometryValid(double viewportWidth,
                                        double viewportHeight,
                                        double trackLeft, double trackTop,
                                        double trackWidth, double trackHeight,
                                        double barWidth, double travelMs,
                                        double startPhase) noexcept
{
  constexpr double kCoordinateSlack = 64.0;
  return std::isfinite(viewportWidth) && viewportWidth >= 1.0 &&
    viewportWidth <= 16384.0 && std::isfinite(viewportHeight) &&
    viewportHeight >= 1.0 && viewportHeight <= 16384.0 &&
    std::isfinite(trackLeft) && trackLeft >= -kCoordinateSlack &&
    std::isfinite(trackTop) && trackTop >= -kCoordinateSlack &&
    std::isfinite(trackWidth) && trackWidth >= 1.0 &&
    trackWidth <= viewportWidth + 2.0 * kCoordinateSlack &&
    std::isfinite(trackHeight) && trackHeight >= 1.0 &&
    trackHeight <= viewportHeight + 2.0 * kCoordinateSlack &&
    trackLeft + trackWidth <= viewportWidth + kCoordinateSlack &&
    trackTop + trackHeight <= viewportHeight + kCoordinateSlack &&
    std::isfinite(barWidth) && barWidth >= 1.0 && barWidth <= 64.0 &&
    std::isfinite(travelMs) && travelMs >= 250.0 && travelMs <= 10000.0 &&
    std::isfinite(startPhase);
}
}
