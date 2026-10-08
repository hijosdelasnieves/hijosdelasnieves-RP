#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace hdn::studio {
enum class Status : std::int32_t {
  closed = 0, loading = 1, captured = 2, rendered = 3,
  unavailable = -1, busy = -2, invalidModel = -3, expired = -4
};

// Called only while the backend mutex is held. No engine pointers or clocks.
class Policy {
public:
  static constexpr std::uint64_t leaseMs = 3000;
  std::int32_t begin(std::uint64_t now) {
    // Never recycle a token: a delayed End from an older UI cannot close a new UI.
    if (serial_ == std::numeric_limits<std::int32_t>::max()) return 0;
    token_ = ++serial_;
    revision_ = 0;
    deadline_ = now + leaseMs;
    status_ = Status::loading;
    yaw_ = 0;
    zoom_ = 1;
    return token_;
  }
  bool live(std::int32_t token, std::uint64_t now) const {
    return token > 0 && token == token_ && now < deadline_;
  }
  bool frame(std::int32_t token, std::uint64_t now, float yaw, float zoom) {
    if (!live(token, now) || !std::isfinite(yaw) || !std::isfinite(zoom)) return false;
    yaw_ = std::remainder(yaw, 360.0f);
    zoom_ = std::clamp(zoom, 0.75f, 1.35f);
    deadline_ = now + leaseMs;
    return true;
  }
  bool select(std::int32_t token, std::int32_t revision, std::uint64_t now) {
    if (!live(token, now) || revision <= revision_) return false;
    revision_ = revision;
    status_ = Status::loading;
    return true;
  }
  bool commit(std::int32_t token, std::int32_t revision, std::uint64_t now, bool valid) {
    if (!live(token, now) || revision != revision_) return false;
    status_ = valid ? Status::captured : Status::invalidModel;
    return true;
  }
  bool end(std::int32_t token) {
    if (token <= 0 || token != token_) return false;
    token_ = 0;
    status_ = Status::closed;
    return true;
  }
  bool expire(std::uint64_t now) {
    if (!token_ || now < deadline_) return false;
    token_ = 0;
    status_ = Status::expired;
    return true;
  }
  void rendered(bool available) {
    if (status_ == Status::captured || status_ == Status::rendered || status_ == Status::busy)
      status_ = available ? Status::rendered : Status::busy;
  }
  Status status(std::int32_t token, std::uint64_t now) const {
    return live(token, now) ? status_ : Status::closed;
  }
  std::int32_t token() const { return token_; }
  float yaw() const { return yaw_; }
  float zoom() const { return zoom_; }
private:
  std::int32_t serial_ = 0, token_ = 0, revision_ = 0;
  std::uint64_t deadline_ = 0;
  Status status_ = Status::closed;
  float yaw_ = 0, zoom_ = 1;
};

struct Rig { float x, y, z, radius; };
// Inventory camera looks down -Y; an upright actor at yaw zero faces +Y.
// Keep the sphere inside the preview column at every aspect ratio.
inline Rig fit(float radius, float aspect, float centerX, float centerY, float zoom) {
  if (!(radius > 0) || !std::isfinite(radius) || !(aspect > 0) || !std::isfinite(aspect)) return {};
  const float safeZoom = std::clamp(zoom, 0.75f, 1.35f);
  const float halfHeight = std::tan(20.0f * 3.14159265358979323846f / 180.0f);
  const float distance = radius * 1.18f / (halfHeight * safeZoom * std::min(0.40f * aspect, 0.80f));
  return {-(centerX * 2 - 1) * distance * halfHeight * aspect,
    -distance, (1 - centerY * 2) * distance * halfHeight, radius};
}
}
