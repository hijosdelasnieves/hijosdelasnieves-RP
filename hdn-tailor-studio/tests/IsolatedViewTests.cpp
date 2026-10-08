#include "IsolatedView.hpp"
#include "Policy.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace hdn::studio::isolated;
int main()
{
  hdn::studio::Policy policy;
  const auto token = policy.begin(0);
  assert(policy.select(token, 1, 1));
  assert(!policy.captureWindow(token, 2, 2));
  assert(policy.captureWindow(token, 1, 2));
  assert(policy.live(token, 12001));
  assert(!policy.live(token, 12002));
  assert(policy.frame(token, 10000, 0, 1));
  assert(!policy.live(token, 13000));
  assert(policy.commit(token, 1, 11000, true));
  assert(!policy.captureWindow(token, 1, 11000));
  std::size_t checks = 0;
  const Bounds body{ { -25, -20, 0 }, { 25, 20, 180 } };
  const Bounds cape{ { -100, -80, -8 }, { 120, 70, 188 } };
  for (const auto bounds : { body, cape }) {
    for (const double aspect : { 0.05, 0.2, 0.35, 0.5, 1., 1.7, 3., 20. }) {
      for (const double zoom : { 0.75, 1., 1.35 }) {
        const auto camera = fitCamera(bounds, aspect, zoom);
        assert(camera);
        for (int yaw = 0; yaw < 360; yaw += 15) {
          const double angle = yaw * 3.14159265358979323846 / 180;
          for (const double x : { bounds.minimum.x, bounds.maximum.x })
            for (const double y : { bounds.minimum.y, bounds.maximum.y })
              for (const double z : { bounds.minimum.z, bounds.maximum.z }) {
                const double px = x - camera->center.x;
                const double py = y - camera->center.y;
                const double pz = z - camera->center.z;
                const double rx = px * std::cos(angle) - py * std::sin(angle);
                const double ry = px * std::sin(angle) + py * std::cos(angle);
                const double depth = camera->distance - ry;
                assert(depth > camera->nearPlane && depth < camera->farPlane);
                assert(std::abs(rx / depth / camera->horizontalTangent) < 1);
                assert(std::abs(pz / depth / camera->verticalTangent) < 1);
                ++checks;
              }
        }
      }
    }
  }
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  assert(!fitCamera(body, nan));
  assert(!fitCamera(body, 0));
  assert(!fitCamera(body, 1, nan));
  assert(!fitCamera({ { 1, 0, 0 }, { 0, 1, 1 } }, 1));
  assert(!fitCamera({ {}, {} }, 1));
  assert(!fitCamera({ {}, { nan, 1, 1 } }, 1));
  Mesh mesh;
  mesh.vertices = { { { 0, 0, 0 }, { 0, 1, 0 }, {} },
                    { { 1, 0, 0 }, { 0, 1, 0 }, {} },
                    { { 0, 0, 1 }, { 0, 1, 0 }, {} } };
  mesh.indices = { 0, 1, 2 };
  assert(validateScene({ mesh }));
  Scene copy{ { mesh }, *validateScene({ mesh }) };
  mesh.vertices[0].position.x = 99;
  assert(copy.meshes[0].vertices[0].position.x == 0);
  mesh.indices[2] = 3;
  assert(!validateScene({ mesh }));
  mesh.indices = { 0, 1 };
  assert(!validateScene({ mesh }));
  assert(!validateScene({}));
  mesh.indices = { 0, 1, 2 };
  mesh.vertices[0].normal.x = nan;
  assert(!validateScene({ mesh }));
  mesh.vertices[0].normal.x = 0;
  mesh.vertices[0].position.z = 10001;
  assert(!validateScene({ mesh }));
  std::cout << "PASS: " << checks
            << " projected corners; malformed data and scene ownership\n";
}
