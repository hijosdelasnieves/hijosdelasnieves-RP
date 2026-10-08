#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace hdn::studio {
enum class Status : std::int32_t
{
  closed = 0,
  loading = 1,
  captured = 2,
  rendered = 3,
  unavailable = -1,
  busy = -2,
  invalidModel = -3,
  expired = -4
};
struct Viewport
{
  float x = 0.5f, y = 0.5f, width = 0.28f, height = 0.70f;
};

// Called only while the backend mutex is held. No engine pointers or clocks.
class Policy
{
public:
  static constexpr std::uint64_t leaseMs = 3000;
  std::int32_t begin(std::uint64_t now)
  {
    // Never recycle a token: a delayed End from an older UI cannot close a new
    // UI.
    if (serial_ == std::numeric_limits<std::int32_t>::max())
      return 0;
    token_ = ++serial_;
    revision_ = 0;
    deadline_ = now + leaseMs;
    status_ = Status::loading;
    yaw_ = 0;
    zoom_ = 1;
    return token_;
  }
  bool live(std::int32_t token, std::uint64_t now) const
  {
    return token > 0 && token == token_ && now < deadline_;
  }
  bool frame(std::int32_t token, std::uint64_t now, float yaw, float zoom)
  {
    if (!live(token, now) || !std::isfinite(yaw) || !std::isfinite(zoom))
      return false;
    yaw_ = std::remainder(yaw, 360.0f);
    zoom_ = std::clamp(zoom, 0.75f, 1.35f);
    deadline_ = now + leaseMs;
    return true;
  }
  bool viewport(std::int32_t token, std::uint64_t now, Viewport value)
  {
    if (!live(token, now) || !std::isfinite(value.x) ||
        !std::isfinite(value.y) || !std::isfinite(value.width) ||
        !std::isfinite(value.height) || value.width < 0.02f ||
        value.height < 0.02f || value.x - value.width / 2 < 0 ||
        value.x + value.width / 2 > 1 || value.y - value.height / 2 < 0 ||
        value.y + value.height / 2 > 1)
      return false;
    viewport_ = value;
    return true;
  }
  bool select(std::int32_t token, std::int32_t revision, std::uint64_t now)
  {
    if (!live(token, now) || revision <= revision_)
      return false;
    revision_ = revision;
    status_ = Status::loading;
    return true;
  }
  // Independent resource capture runs on the main thread and can stall the
  // client's own update/heartbeat. Give ONLY a current loading revision a
  // bounded initial window; normal Frame() returns to the 3-second lease.
  bool captureWindow(std::int32_t token, std::int32_t revision,
                     std::uint64_t now)
  {
    if (!live(token, now) || revision != revision_ ||
        status_ != Status::loading)
      return false;
    deadline_ = now + 12000;
    return true;
  }
  bool commit(std::int32_t token, std::int32_t revision, std::uint64_t now,
              bool valid)
  {
    if (!live(token, now) || revision != revision_)
      return false;
    status_ = valid ? Status::captured : Status::invalidModel;
    return true;
  }
  bool rejectCapture(std::int32_t token, std::int32_t revision,
                     std::uint64_t now)
  {
    if (!live(token, now) || revision != revision_ ||
        status_ != Status::loading)
      return false;
    status_ = Status::busy;
    return true;
  }
  bool end(std::int32_t token)
  {
    if (token <= 0 || token != token_)
      return false;
    token_ = 0;
    status_ = Status::closed;
    return true;
  }
  bool expire(std::uint64_t now)
  {
    if (!token_ || now < deadline_)
      return false;
    token_ = 0;
    status_ = Status::expired;
    return true;
  }
  void rendered(bool available)
  {
    if (status_ == Status::captured || status_ == Status::rendered ||
        status_ == Status::busy)
      status_ = available ? Status::rendered : Status::busy;
  }
  void captureFailed()
  {
    if (status_ == Status::captured || status_ == Status::rendered ||
        status_ == Status::busy)
      status_ = Status::invalidModel;
  }
  Status status(std::int32_t token, std::uint64_t now) const
  {
    return live(token, now) ? status_ : Status::closed;
  }
  std::int32_t token() const { return token_; }
  std::int32_t revision() const { return revision_; }
  float yaw() const { return yaw_; }
  float zoom() const { return zoom_; }
  Viewport viewport() const { return viewport_; }

private:
  std::int32_t serial_ = 0, token_ = 0, revision_ = 0;
  std::uint64_t deadline_ = 0;
  Status status_ = Status::closed;
  float yaw_ = 0, zoom_ = 1;
  Viewport viewport_{};
};

struct Rig
{
  float x, y, z, radius;
};
// Inventory camera looks down -Y; an upright actor at yaw zero faces +Y.
// Keep the sphere inside the preview column at every aspect ratio.
inline Rig fitFrustum(float radius, float halfWidth, float halfHeight,
                      Viewport viewport, float zoom)
{
  if (!(radius > 0) || !std::isfinite(radius) || !(halfWidth > 0) ||
      !std::isfinite(halfWidth) || !(halfHeight > 0) ||
      !std::isfinite(halfHeight))
    return {};
  const float safeZoom = std::clamp(zoom, 0.75f, 1.35f);
  const float distance = radius +
    radius * 1.18f /
      (safeZoom *
       std::min(viewport.width * halfWidth, viewport.height * halfHeight));
  return { -(viewport.x * 2 - 1) * distance * halfWidth, -distance,
           (1 - viewport.y * 2) * distance * halfHeight, radius };
}
inline Rig fit(float radius, float aspect, Viewport viewport, float zoom)
{
  const float halfHeight = std::tan(20.0f * 3.14159265358979323846f / 180.0f);
  return fitFrustum(radius, halfHeight * aspect, halfHeight, viewport, zoom);
}
}
