// Value-only skeleton capture. No retained engine object or animation worker.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hdn::studio {
struct PoseTransform
{
  std::array<std::array<double, 3>, 3> rotation{
    { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }
  };
  std::array<double, 3> translation{};
  double scale = 1;
};
struct CapturedBone
{
  std::array<float, 3> translation{};
  std::array<float, 4> rotation{ 0, 0, 0, 1 };
  std::array<float, 3> scale{ 1, 1, 1 };
};
inline bool validPoseTransform(const PoseTransform& value)
{
  if (!std::isfinite(value.scale) || value.scale <= 0 || value.scale > 100)
    return false;
  for (const auto coordinate : value.translation)
    if (!std::isfinite(coordinate) || std::abs(coordinate) > 100000000)
      return false;
  for (size_t row = 0; row < 3; ++row) {
    double length = 0;
    for (const auto component : value.rotation[row]) {
      if (!std::isfinite(component))
        return false;
      length += component * component;
    }
    if (std::abs(length - 1) > 0.01)
      return false;
    for (size_t other = row + 1; other < 3; ++other) {
      double dot = 0;
      for (size_t column = 0; column < 3; ++column)
        dot += value.rotation[row][column] * value.rotation[other][column];
      if (std::abs(dot) > 0.01)
        return false;
    }
  }
  return true;
}
inline bool rootRelativePose(const PoseTransform& root,
                             const PoseTransform& world, CapturedBone& bone)
{
  if (!validPoseTransform(root) || !validPoseTransform(world))
    return false;
  std::array<std::array<double, 3>, 3> matrix{};
  for (size_t row = 0; row < 3; ++row) {
    double position = 0;
    for (size_t k = 0; k < 3; ++k) {
      position += root.rotation[k][row] *
        (world.translation[k] - root.translation[k]) / root.scale;
      for (size_t column = 0; column < 3; ++column)
        matrix[row][column] +=
          root.rotation[k][row] * world.rotation[k][column];
    }
    if (!std::isfinite(position) || std::abs(position) > 10000)
      return false;
    bone.translation[row] = static_cast<float>(position);
  }
  const double scale = world.scale / root.scale;
  if (!std::isfinite(scale) || scale <= 0 || scale > 10)
    return false;
  bone.scale.fill(static_cast<float>(scale));
  // Stable matrix -> xyzw quaternion, including 180-degree rotations.
  std::array<double, 4> q{};
  const double trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
  if (trace > 0) {
    const double s = std::sqrt(trace + 1) * 2;
    q = { (matrix[2][1] - matrix[1][2]) / s, (matrix[0][2] - matrix[2][0]) / s,
          (matrix[1][0] - matrix[0][1]) / s, s / 4 };
  } else {
    size_t i = 0;
    if (matrix[1][1] > matrix[i][i])
      i = 1;
    if (matrix[2][2] > matrix[i][i])
      i = 2;
    const size_t j = (i + 1) % 3, k = (i + 2) % 3;
    const double s =
      std::sqrt(1 + matrix[i][i] - matrix[j][j] - matrix[k][k]) * 2;
    q[i] = s / 4;
    q[j] = (matrix[j][i] + matrix[i][j]) / s;
    q[k] = (matrix[k][i] + matrix[i][k]) / s;
    q[3] = (matrix[k][j] - matrix[j][k]) / s;
  }
  double length = 0;
  for (const auto component : q)
    length += component * component;
  if (!std::isfinite(length) || length < 0.000001)
    return false;
  for (size_t i = 0; i < 4; ++i)
    bone.rotation[i] = static_cast<float>(q[i] / std::sqrt(length));
  return true;
}
class CapturedPose
{
public:
  static constexpr size_t maxBones = 4096;
  std::vector<std::string> names;
  std::vector<std::int16_t> parents;
  std::vector<CapturedBone> transforms;

  bool add(std::string_view name, const PoseTransform& root,
           const PoseTransform& world)
  {
    if (name.empty() || name.size() > 255)
      return false;
    for (const auto& existing : names)
      if (existing == name)
        return true;
    if (names.size() >= maxBones)
      return false;
    CapturedBone bone;
    if (!rootRelativePose(root, world, bone))
      return false;
    names.emplace_back(name);
    // Already root-local GLOBAL frames: independent of engine entry order,
    // and no second parent composition when feeding SetBoneLocalPose.
    parents.push_back(-1);
    transforms.push_back(bone);
    return true;
  }
};
}
