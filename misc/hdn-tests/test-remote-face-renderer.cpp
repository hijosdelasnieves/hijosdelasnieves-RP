#include "test-renderer-stubs/MockCommonLib.h"
#include "RemoteFaceTintRenderer.h"
#include <iostream>

namespace Frida {
void ApplyRemoteFaceTintMasks(std::uint32_t actorId,
                             const RE::BSTArray<RE::TintMask*>& masks,
                             RE::NiSourceTexture* texture)
{
  assert(actorId >= 0xff000000 && !masks.empty());
  assert(texture && texture->rendererTexture);
  ++Test::renderCalls;
  if (Test::onRender) Test::onRender();
}
}

int main()
{
  using Result = Hdn::RemoteFaceTintResult;
  RE::BSGraphics::Renderer renderer;
  RE::BSGraphics::Renderer::singleton = &renderer;
  RE::TESNPC base;
  RE::BSLightingShaderMaterialFacegen material, materialB;
  RE::BSLightingShaderProperty property;
  property.material = &material;
  RE::BSGeometry geometry, otherGeometry;
  geometry.runtime.properties[1].reset(&property);
  RE::Actor actor;
  actor.base = &base; actor.head = &geometry;
  RE::TintMask mask;
  RE::BSTArray<RE::TintMask*> masks{ &mask };
  auto run = [&] { return Hdn::RenderRemoteFaceTints(&actor, base.id, masks); };

  assert(Hdn::RenderRemoteFaceTints(nullptr, base.id, masks) == Result::Drop);
  actor.id = 0x14; assert(run() == Result::Drop); actor.id = 0xff000200;
  base.id = 7; assert(run() == Result::Drop); base.id = 0xff000100;
  assert(Hdn::RenderRemoteFaceTints(&actor, base.id + 1, masks) == Result::Drop);
  masks.clear(); assert(run() == Result::Drop); masks.push_back(nullptr);
  assert(run() == Result::Drop); masks[0] = &mask;
  actor.deleted = true; assert(run() == Result::Drop); actor.deleted = false;
  actor.marked = true; assert(run() == Result::Drop); actor.marked = false;
  Test::ae = false; assert(run() == Result::Drop); Test::ae = true;
  actor.loaded = false; assert(run() == Result::Retry); actor.loaded = true;
  actor.head = nullptr; assert(run() == Result::Retry); actor.head = &geometry;
  property.material = nullptr; assert(run() == Result::Retry); property.material = &material;
  material.feature = RE::BSShaderMaterial::Feature::kFaceGenRGBTint;
  assert(run() == Result::Drop); assert(!material.tintTexture);
  material.feature = RE::BSShaderMaterial::Feature::kFaceGen;
  assert(Test::factoryCalls == 0);

  RE::BSGraphics::Renderer::singleton = nullptr; assert(run() == Result::Retry);
  RE::BSGraphics::Renderer::singleton = &renderer;
  Test::factoryFails = true; assert(run() == Result::Retry); Test::factoryFails = false;
  Test::resourceFails = true; assert(run() == Result::Retry); Test::resourceFails = false;
  assert(Test::liveTextures == 0 && Test::liveResources == 0);
  Test::factoryHasResource = true; assert(run() == Result::Retry); Test::factoryHasResource = false;
  assert(Test::liveTextures == 0 && Test::liveResources == 0);

  assert(run() == Result::Applied);
  assert(material.tintTexture && Test::liveTextures == 1 && Test::liveResources == 1);
  auto oldTexture = material.tintTexture.get();
  Test::onRender = [&] { actor.head = &otherGeometry; };
  assert(run() == Result::Retry);
  assert(material.tintTexture.get() == oldTexture);
  assert(Test::liveTextures == 1 && Test::liveResources == 1);
  actor.head = &geometry;
  Test::onRender = [&] { property.material = &materialB; };
  assert(run() == Result::Retry);
  assert(material.tintTexture.get() == oldTexture && !materialB.tintTexture);
  assert(Test::liveTextures == 1 && Test::liveResources == 1);
  property.material = &material;
  Test::onRender = {};

  for (int i = 0; i < 1000; ++i) {
    assert(run() == Result::Applied);
    assert(Test::liveTextures == 1 && Test::liveResources == 1);
  }
  material.tintTexture.reset();
  assert(Test::liveTextures == 0 && Test::liveResources == 0);
  assert(base.id == 0xff000100 && actor.id == 0xff000200);
  assert(property.material == &material && material.refs == 0);
  std::cout << "PASS: actor/base/mask guards, shader 5 rejected, readiness, OOM, stale geometry/material, 1000 GPU replacements without retained resources\n";
}
