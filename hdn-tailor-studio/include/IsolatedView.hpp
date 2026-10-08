#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hdn::studio::isolated {
// This scene owns plain values only. Never store NiObjects, Actor*, engine
// geometry buffers or inventory/UI3D resources in the renderer's scene.
struct Point
{
  double x = 0, y = 0, z = 0;
};
inline bool finite(Point p)
{
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
struct Bounds
{
  Point minimum, maximum;
};
struct Camera
{
  Point center;
  double radius, distance, nearPlane, farPlane, verticalTangent,
    horizontalTangent;
};
// Coordinates are independent of the gameplay camera. All yaw angles use
// the same bounding sphere: rotating a cape cannot invalidate the fit.
inline std::optional<Camera> fitCamera(Bounds bounds, double aspect,
                                       double zoom = 1)
{
  if (!finite(bounds.minimum) || !finite(bounds.maximum) ||
      bounds.minimum.x > bounds.maximum.x ||
      bounds.minimum.y > bounds.maximum.y ||
      bounds.minimum.z > bounds.maximum.z || !std::isfinite(aspect) ||
      aspect < 0.05 || aspect > 20 || !std::isfinite(zoom))
    return std::nullopt;
  Camera result{};
  result.center = { bounds.minimum.x / 2 + bounds.maximum.x / 2,
                    bounds.minimum.y / 2 + bounds.maximum.y / 2,
                    bounds.minimum.z / 2 + bounds.maximum.z / 2 };
  result.radius = std::hypot(bounds.maximum.x / 2 - bounds.minimum.x / 2,
                             bounds.maximum.y / 2 - bounds.minimum.y / 2,
                             bounds.maximum.z / 2 - bounds.minimum.z / 2);
  if (!std::isfinite(result.radius) || result.radius <= 0.0001 ||
      result.radius > 10000)
    return std::nullopt;
  constexpr double pi = 3.14159265358979323846;
  result.verticalTangent = std::tan(20 * pi / 180);
  result.horizontalTangent = result.verticalTangent * aspect;
  const double halfAngle =
    std::atan(std::min(result.verticalTangent, result.horizontalTangent));
  // Full-avatar fit remains safe even at maximum zoom. A smaller zoom pulls
  // back; this default mode never crops head/feet to enlarge the model.
  const double safeZoom = std::clamp(zoom, 0.75, 1.0);
  result.distance = result.radius * 1.12 / (std::sin(halfAngle) * safeZoom);
  result.nearPlane = std::max(0.001, (result.distance - result.radius) * 0.5);
  result.farPlane = result.distance + result.radius * 2;
  return result;
}

struct Vertex
{
  Point position, normal;
  std::array<float, 2> uv{};
};
struct Mesh
{
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
};
struct Scene
{
  std::vector<Mesh> meshes;
  Bounds bounds;
};
// Reject incomplete/corrupt buffers before uploading to the independent GPU
// renderer. One scene's budget includes ALL mesh parts, not each part alone.
inline std::optional<Bounds> validateScene(const std::vector<Mesh>& meshes)
{
  constexpr std::size_t maximumVertices = 500000;
  constexpr std::size_t maximumIndices = 3000000;
  if (meshes.empty() || meshes.size() > 256)
    return std::nullopt;
  std::size_t vertices = 0, indices = 0;
  std::optional<Bounds> bounds;
  for (const auto& mesh : meshes) {
    if (mesh.vertices.empty() || mesh.indices.empty() ||
        mesh.indices.size() % 3 != 0 ||
        mesh.vertices.size() > maximumVertices - vertices ||
        mesh.indices.size() > maximumIndices - indices)
      return std::nullopt;
    vertices += mesh.vertices.size();
    indices += mesh.indices.size();
    for (const auto& vertex : mesh.vertices) {
      if (!finite(vertex.position) || !finite(vertex.normal) ||
          !std::isfinite(vertex.uv[0]) || !std::isfinite(vertex.uv[1]) ||
          std::abs(vertex.position.x) > 10000 ||
          std::abs(vertex.position.y) > 10000 ||
          std::abs(vertex.position.z) > 10000)
        return std::nullopt;
      if (!bounds)
        bounds = Bounds{ vertex.position, vertex.position };
      bounds->minimum.x = std::min(bounds->minimum.x, vertex.position.x);
      bounds->minimum.y = std::min(bounds->minimum.y, vertex.position.y);
      bounds->minimum.z = std::min(bounds->minimum.z, vertex.position.z);
      bounds->maximum.x = std::max(bounds->maximum.x, vertex.position.x);
      bounds->maximum.y = std::max(bounds->maximum.y, vertex.position.y);
      bounds->maximum.z = std::max(bounds->maximum.z, vertex.position.z);
    }
    for (const auto index : mesh.indices)
      if (index >= mesh.vertices.size())
        return std::nullopt;
  }
  if (!bounds || !fitCamera(*bounds, 1))
    return std::nullopt;
  return bounds;
}
}
