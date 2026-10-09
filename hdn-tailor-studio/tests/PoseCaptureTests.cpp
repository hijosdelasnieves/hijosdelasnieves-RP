#include "PoseCapture.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace hdn::studio;
using Matrix = std::array<std::array<double, 3>, 3>;
size_t checks = 0;
void near(double actual, double expected)
{
  assert(std::abs(actual - expected) < 0.0001);
  ++checks;
}
PoseTransform compose(const PoseTransform& root, const PoseTransform& local)
{
  PoseTransform result;
  result.scale = root.scale * local.scale;
  result.translation = root.translation;
  for (size_t i = 0; i < 3; ++i) {
    for (size_t j = 0; j < 3; ++j) {
      result.rotation[i][j] = 0;
      for (size_t k = 0; k < 3; ++k)
        result.rotation[i][j] += root.rotation[i][k] * local.rotation[k][j];
      result.translation[i] +=
        root.rotation[i][j] * local.translation[j] * root.scale;
    }
  }
  return result;
}
Matrix quaternionMatrix(const CapturedBone& bone)
{
  const double x = bone.rotation[0], y = bone.rotation[1];
  const double z = bone.rotation[2], w = bone.rotation[3];
  return {
    { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
      { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
      { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } }
  };
}
int main()
{
  const Matrix identity{ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
  const Matrix yaw90{ { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } } };
  const Matrix yaw180{ { { -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } } };
  const Matrix pitch180{ { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 } } };
  const Matrix roll180{ { { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } } };
  const Matrix roll90{ { { 0, 0, 1 }, { 0, 1, 0 }, { -1, 0, 0 } } };
  for (const auto worldRotation :
       { identity, yaw90, yaw180, pitch180, roll180 }) {
    for (const double scale : { 0.5, 1., 2. }) {
      PoseTransform root;
      root.rotation = worldRotation;
      root.scale = scale;
      root.translation = { 65000, -13000, 2000 };
      for (const auto boneRotation :
           { identity, yaw90, yaw180, pitch180, roll180, roll90 }) {
        PoseTransform local;
        local.rotation = boneRotation;
        local.translation = { -42, 7, 118 };
        local.scale = 0.95;
        CapturedBone bone;
        assert(rootRelativePose(root, compose(root, local), bone));
        const auto matrix = quaternionMatrix(bone);
        for (size_t i = 0; i < 3; ++i) {
          near(bone.translation[i], local.translation[i]);
          near(bone.scale[i], local.scale);
          for (size_t j = 0; j < 3; ++j)
            near(matrix[i][j], boneRotation[i][j]);
        }
      }
    }
  }
  PoseTransform root;
  root.rotation = yaw90;
  root.scale = 2;
  root.translation = { 320, -126, 68 };
  CapturedPose pose;
  // Bone entries need not be parent-before-child; all frames already global.
  // Virtual fingers/twist/cloth have no NiAVObject node, but are still copied.
  const std::array names{ "NPC L Finger12 [LF12]",
                          "NPC R ForearmTwist1 [Rft1]", "NPC L Forearm [LLar]",
                          "NPC Head [Head]", "SkirtBone01" };
  for (size_t i = 0; i < names.size(); ++i) {
    PoseTransform local;
    local.translation = { static_cast<double>(i * 10), 4, 100 };
    local.rotation = roll90;
    assert(pose.add(names[i], root, compose(root, local)));
    assert(pose.parents[i] == -1);
    assert(pose.names[i] == names[i]);
    near(pose.transforms[i].translation[0], i * 10);
  }
  assert(pose.add(names[0], root, root));
  assert(pose.names.size() ==
         names.size()); // duplicate hierarchy nodes ignored
  const auto owned = pose.transforms;
  root.translation[0] = 9000;
  near(pose.transforms[0].translation[0], owned[0].translation[0]);
  // Finger skin point follows its own frame, NOT the fallback head frame.
  const auto fingerMatrix = quaternionMatrix(pose.transforms[0]);
  near(fingerMatrix[2][0] * 3 + pose.transforms[0].translation[2], 97);
  assert(!pose.add("", root, root));
  assert(!pose.add(std::string(256, 'a'), root, root));
  CapturedBone bone;
  for (const double bad :
       { 0., -1., 101., std::numeric_limits<double>::quiet_NaN() }) {
    PoseTransform invalid;
    invalid.scale = bad;
    assert(!rootRelativePose(invalid, root, bone));
    assert(!rootRelativePose(root, invalid, bone));
  }
  PoseTransform invalid;
  invalid.translation[2] = std::numeric_limits<double>::infinity();
  assert(!rootRelativePose(root, invalid, bone));
  invalid = {};
  invalid.rotation[1][0] = 1;
  assert(!rootRelativePose(root, invalid, bone));
  invalid = {};
  invalid.translation[0] = 10001;
  assert(!rootRelativePose(PoseTransform{}, invalid, bone));
  CapturedPose large;
  for (size_t i = 0; i < CapturedPose::maxBones; ++i)
    assert(
      large.add("bone" + std::to_string(i), PoseTransform{}, PoseTransform{}));
  assert(!large.add("overflow", PoseTransform{}, PoseTransform{}));
  std::cout << "PASS: " << checks
            << " pose values; virtual bones, arbitrary order, ownership and "
               "malformed input\n";
}
