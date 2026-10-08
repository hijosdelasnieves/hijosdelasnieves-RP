// HDN independent mesh fork, GPL-3.0; see THIRD-PARTY.txt.
#pragma once
#include "IsolatedView.hpp"
#include "Mesh.h"
#include "RenderManager.h"

namespace hdn::studio {
inline thread_local float independentAspect = 0;
class AspectScope
{
  float previous_;

public:
  explicit AspectScope(float aspect)
    : previous_(independentAspect)
  {
    independentAspect = aspect;
  }
  ~AspectScope() { independentAspect = previous_; }
  AspectScope(const AspectScope&) = delete;
  AspectScope& operator=(const AspectScope&) = delete;
};
inline float frameworkAspect(const RenderTarget* target)
{
  return independentAspect > 0
    ? independentAspect
    : static_cast<float>(target->width) / target->height;
}
inline std::optional<isolated::Camera> frameworkCamera(
  const Mesh* mesh, const RenderTarget* target)
{
  if (!mesh || !target || !target->width || !target->height ||
      target->width > 2048 || target->height > 2048 || mesh->parts.empty() ||
      mesh->parts.size() > 256)
    return {};
  isolated::Bounds bounds{};
  bool first = true;
  size_t vertices = 0, indices = 0;
  for (const auto& part : mesh->parts) {
    vertices += part.vertices.size();
    indices += part.indices.size();
    if (vertices > 500000 || indices > 3000000 || part.vertices.empty() ||
        part.indices.empty() || part.indices.size() % 3)
      return {};
    for (const auto index : part.indices)
      if (index >= part.vertices.size())
        return {};
    for (const auto& vertex : part.vertices) {
      const isolated::Point p{ vertex.position[0], vertex.position[1],
                               vertex.position[2] };
      if (!isolated::finite(p) || std::abs(p.x) > 10000 ||
          std::abs(p.y) > 10000 || std::abs(p.z) > 10000)
        return {};
      for (const float n : vertex.normal)
        if (!std::isfinite(n))
          return {};
      for (const float uv : vertex.uv)
        if (!std::isfinite(uv))
          return {};
      if (first) {
        bounds = { p, p };
        first = false;
      } else {
        bounds.minimum = { std::min(bounds.minimum.x, p.x),
                           std::min(bounds.minimum.y, p.y),
                           std::min(bounds.minimum.z, p.z) };
        bounds.maximum = { std::max(bounds.maximum.x, p.x),
                           std::max(bounds.maximum.y, p.y),
                           std::max(bounds.maximum.z, p.z) };
      }
    }
  }
  return isolated::fitCamera(bounds, frameworkAspect(target));
}
}
