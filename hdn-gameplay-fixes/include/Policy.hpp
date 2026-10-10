#pragma once
#include <cmath>
#include <cstdint>

namespace hdn::gameplay {
inline bool supportedRuntime(unsigned major, unsigned minor, unsigned build,
                             unsigned sub)
{
  return major == 1 && minor == 6 && build == 1170 && sub == 0;
}
inline int cellOf(float coordinate)
{
  return static_cast<int>(std::floor(coordinate / 4096.0f));
}
inline bool validDestination(float x, float y, float z)
{
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
    std::abs(x) < 1.0e7f && std::abs(y) < 1.0e7f && std::abs(z) < 1.0e7f;
}
inline bool atDestination(std::uint32_t actualWorld, float x, float y,
                          std::uint32_t requestedWorld, float targetX,
                          float targetY)
{
  return actualWorld && actualWorld == requestedWorld &&
    std::abs(x - targetX) <= 512 && std::abs(y - targetY) <= 512;
}
enum class InputMode : unsigned { Idle, Chat, AwaitNeutral };
// Closing never resumes held input. A complete neutral sample is required.
class InputGate {
public:
  InputMode mode{ InputMode::Idle };
  void open() { mode = InputMode::Chat; }
  void close()
  {
    if (mode == InputMode::Chat)
      mode = InputMode::AwaitNeutral;
  }
  bool neutral(bool allReleased)
  {
    if (mode != InputMode::AwaitNeutral || !allReleased)
      return false;
    mode = InputMode::Idle;
    return true;
  }
};
}  // namespace hdn::gameplay
