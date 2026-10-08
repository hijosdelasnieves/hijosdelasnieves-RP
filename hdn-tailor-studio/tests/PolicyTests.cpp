#include "PixelProbe.hpp"
#include "Policy.hpp"
#include "RenderDiagnostic.hpp"
#include "Runtime.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

int checks = 0;
void check(bool condition)
{
  ++checks;
  if (!condition) {
    std::cerr << "Failed check " << checks << '\n';
    std::exit(1);
  }
}
int main()
{
  using namespace hdn::studio;
  check(supportedRuntime({ 1, 5, 97, 0 }));
  check(supportedRuntime({ 1, 6, 1170, 0 }));
  check(!supportedRuntime({ 1, 6, 1170, 1 }));
  check(!supportedRuntime({ 1, 6, 1179, 0 }));
  check(!supportedRuntime({ 1, 6, 1130, 0 }));
  check(!supportedRuntime({ 1, 7, 104, 0 }));
  check(!supportedRuntime({ 1, 4, 15, 0 }));
  const SceneState idle{ .ui = true, .camera = true, .inventory = true };
  check(sceneReason(idle) == RenderReason::none);
  for (auto [field, reason] : std::array{
         std::pair{ &SceneState::ui, RenderReason::missingUI },
         std::pair{ &SceneState::camera, RenderReason::missingCamera },
         std::pair{ &SceneState::inventory,
                    RenderReason::missingInventory } }) {
    auto state = idle;
    state.*field = false;
    check(sceneReason(state) == reason);
  }
  for (auto [field, reason] : std::array{
         std::pair{ &SceneState::closing, RenderReason::closingMenus },
         std::pair{ &SceneState::loadTask, RenderReason::loadTask },
         std::pair{ &SceneState::temporaryReference,
                    RenderReason::temporaryReference } }) {
    auto state = idle;
    state.*field = true;
    check(sceneReason(state) == reason);
  }
  for (auto [field, reason] : std::array{
         std::pair{ &SceneState::pauses, RenderReason::pausedUI },
         std::pair{ &SceneState::itemMenus, RenderReason::itemMenu },
         std::pair{ &SceneState::customRendering,
                    RenderReason::customRendering },
         std::pair{ &SceneState::loadedModels, RenderReason::loadedModels },
         std::pair{ &SceneState::lightSchemes, RenderReason::lightSchemes },
         std::pair{ &SceneState::menuObjects, RenderReason::menuObjects } }) {
    auto state = idle;
    state.*field = 1;
    check(sceneReason(state) == reason);
  }
  for (int value = 0; value <= static_cast<int>(RenderReason::noGeometry);
       ++value) {
    const auto name = reasonName(static_cast<RenderReason>(value));
    check(!name.empty() && name != "unknown");
  }
  Policy p;
  check(!p.live(0, 0));
  const auto first = p.begin(100);
  check(first > 0 && p.live(first, 100));
  check(p.status(first, 100) == Status::loading);
  check(!p.select(first, 0, 100));
  check(p.select(first, 1, 100));
  check(p.commit(first, 1, 100, true));
  check(p.status(first, 100) == Status::captured);
  p.rendered(true);
  check(p.status(first, 100) == Status::rendered);
  p.rendered(false);
  check(p.status(first, 100) == Status::busy);
  p.rendered(true);
  check(p.status(first, 100) == Status::rendered);
  check(p.select(first, 2, 101));
  check(!p.select(first, 1, 101));
  check(!p.commit(first, 1, 101, true));
  check(p.commit(first, 2, 101, false));
  check(p.status(first, 101) == Status::invalidModel);
  check(p.frame(first, 102, 750, 99));
  check(p.viewport(first, 102, { 0.51f, 0.52f, 0.3f, 0.6f }));
  check(!p.viewport(first, 102, { 0, 0, 0.3f, 0.6f }));
  check(!p.viewport(first, 102, { 0.5f, 0.5f, 0, 0.6f }));
  check(!p.viewport(
    first, 102, { 0.5f, 0.5f, std::numeric_limits<float>::infinity(), 0.6f }));
  check(!p.viewport(first + 1, 102, { 0.5f, 0.5f, 0.3f, 0.6f }));
  check(p.yaw() == 30 && p.zoom() == 1.35f);
  check(!p.frame(first, 102, std::numeric_limits<float>::quiet_NaN(), 1));
  check(!p.frame(first, 102, 0, std::numeric_limits<float>::infinity()));
  check(!p.expire(3101));
  check(p.expire(3102));
  check(!p.frame(first, 3103, 0, 1));
  const auto second = p.begin(4000);
  check(second > first);
  check(!p.end(first));
  check(!p.commit(first, 2, 4001, true));
  check(p.live(second, 4001));
  check(p.end(second));
  check(!p.end(second));
  const auto third = p.begin(5000);
  check(p.select(third, 1, 5000) && p.commit(third, 1, 5000, true));
  p.captureFailed();
  check(p.status(third, 5000) == Status::invalidModel);
  p.rendered(true);
  check(p.status(third, 5000) == Status::invalidModel);
  std::array<std::uint8_t, 16 * 4> pixels{};
  check(!hasVisiblePixels(nullptr, 64, 16, 1));
  check(!hasVisiblePixels(pixels.data(), 60, 16, 1));
  check(!hasVisiblePixels(pixels.data(), 64, 16, 1));
  for (unsigned i = 0; i < 16; ++i)
    pixels[i * 4 + 3] = 255;
  check(!hasVisiblePixels(pixels.data(), 64, 16, 1));
  for (unsigned i = 0; i < 15; ++i)
    pixels[i * 4] = 10;
  check(!hasVisiblePixels(pixels.data(), 64, 16, 1));
  pixels[15 * 4] = 10;
  check(hasVisiblePixels(pixels.data(), 64, 16, 1));
  check(hasVisiblePixels(pixels.data(), 32, 8, 2));
  for (float aspect : { 0.6f, 1.0f, 1.7777778f, 2.4f, 3.55f }) {
    auto rig = fit(90, aspect, { 0.5f, 0.5f, 0.3f, 0.6f }, 1);
    check(std::isfinite(rig.x) && std::isfinite(rig.y) && rig.y < -90);
    check(rig.z == 0);
    check(fit(90, aspect, { 0.5f, 0.5f, 0.3f, 0.6f }, 1.35f).y > rig.y);
  }
  check(fit(0, 1, {}, 1).radius == 0);
  check(fit(90, 0, {}, 1).radius == 0);
  std::cout << checks << " policy checks passed (not a Skyrim render test)\n";
}
