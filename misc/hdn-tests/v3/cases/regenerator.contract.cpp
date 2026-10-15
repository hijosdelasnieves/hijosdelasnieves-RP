#include "MockEngine.h"
#include "RemoteFaceRegen.h"
#include <iostream>
#include <stdexcept>

int main()
{
  using Scope = Hdn::RemoteFaceTintScope;
  using Result = Hdn::RemoteFaceRegenQueue::Result;
  RE::Actor actor(0xff000100, 0xff000900);
  RE::BSFaceGenManager manager;
  RE::BSFaceGenManager::singleton = &manager;
  RE::TintMask mask{11};
  auto writable = std::make_shared<Hdn::RemoteFaceTintSnapshot>();
  writable->masks.push_back(&mask);
  std::shared_ptr<const Hdn::RemoteFaceTintSnapshot> masks = writable;
  auto run = [&] { return Hdn::RegenerateRemoteFace(&actor, actor.base.formID, masks); };
  auto clean = [&] { assert(!Scope::Current() && Test::engineCalls == 0); };

  const auto rejected = Hdn::RegenerateRemoteFace(nullptr, actor.base.formID, masks);
  assert(rejected.result == Result::Drop && !rejected.engineCalled);
  actor.formID = 0x14; assert(run().result == Result::Drop); actor.formID = 0xff000100;
  actor.base.formID = 7; assert(run().result == Result::Drop); actor.base.formID = 0xff000900;
  assert(Hdn::RegenerateRemoteFace(&actor, actor.base.formID + 1, masks).result == Result::Drop);
  actor.hasBase = false; assert(run().result == Result::Drop); actor.hasBase = true;
  actor.deleted = true; assert(run().result == Result::Drop); actor.deleted = false;
  actor.marked = true; assert(run().result == Result::Drop); actor.marked = false;
  Test::ae = false; assert(run().result == Result::Drop); Test::ae = true;
  REL::Module::runtimeVersion = REL::Version(1, 6, 640, 0);
  assert(run().result == Result::Drop);
  REL::Module::runtimeVersion = REL::Version(1, 6, 1170, 0);
  assert(Hdn::RegenerateRemoteFace(&actor, actor.base.formID, {}).result == Result::Drop);
  writable->masks.clear(); assert(run().result == Result::Drop);
  writable->masks.push_back(nullptr); assert(run().result == Result::Drop);
  writable->masks[0] = &mask;
  actor.loaded = false;
  const auto unready = run();
  assert(unready.result == Result::Retry && !unready.engineCalled);
  actor.loaded = true;
  actor.hasFace = false; assert(run().result == Result::Retry); actor.hasFace = true;
  actor.base.hasFacePart = false; assert(run().result == Result::Retry); actor.base.hasFacePart = true;
  RE::BSFaceGenManager::singleton = nullptr; assert(run().result == Result::Retry);
  RE::BSFaceGenManager::singleton = &manager;
  clean();
  std::cout << "PASS regenerator: exact runtime, dynamic identity/base, mask, deletion and readiness guards\n";

  RE::BSLightingShaderMaterialFacegen faceMaterial;
  RE::BSLightingShaderProperty lighting;
  RE::BSGeometry head;
  lighting.material = &faceMaterial;
  head.runtime.properties[1].value = &lighting;
  actor.head = &head;
  Test::onEngine = [&](auto* node, auto* part, auto* npc) {
    assert(node == &actor.face && part == &actor.base.facePart && npc == &actor.base);
    auto* state = Scope::Current();
    assert(state && state->actorId == actor.formID && state->baseId == actor.base.formID);
    assert(state->snapshot == masks && state->snapshot->masks[0]->token == 11);
    assert(state->shader == 4 && !state->texture);
    state->applyCount += 2;
    faceMaterial.tintTexture = true;
  };
  auto result = run();
  assert(result.result == Result::Done && result.engineCalled && result.applyCount == 2 && !Scope::Current());
  assert(result.before.shader == 4 && !result.before.texture && result.after.texture);
  std::cout << "PASS regenerator: scope active only inside engine call; repeated applies counted; before/after observation\n";

  Test::onEngine = {};
  result = run();
  assert(result.result == Result::Done && result.engineCalled && result.applyCount == 0 && !Scope::Current());
  Test::onEngine = [&](auto, auto, auto) { throw std::runtime_error("engine"); };
  try { (void)run(); assert(false); } catch (const std::runtime_error&) {}
  assert(!Scope::Current());
  std::cout << "PASS regenerator: zero apply still completes one engine call; C++ exception restores scope\n";

  RE::BSShaderMaterial rgbMaterial;
  rgbMaterial.feature = RE::BSShaderMaterial::Feature::kFaceGenRGBTint;
  lighting.material = &rgbMaterial;
  Test::onEngine = [&](auto, auto, auto) { ++Scope::Current()->applyCount; };
  result = run();
  assert(result.result == Result::Done && result.before.shader == 5 && result.after.shader == 5);
  assert(!result.before.texture && !result.after.texture);
  std::cout << "PASS regenerator: shader 5 remains engine-managed and no tintTexture cast is used\n";

  RE::Actor innerActor(0xff000101, 0xff000901);
  RE::TintMask innerMask{22};
  auto innerWritable = std::make_shared<Hdn::RemoteFaceTintSnapshot>();
  innerWritable->masks.push_back(&innerMask);
  std::shared_ptr<const Hdn::RemoteFaceTintSnapshot> innerMasks = innerWritable;
  Test::onEngine = [&](auto, auto, auto* npc) {
    auto* state = Scope::Current();
    if (npc == &actor.base) {
      ++state->applyCount;
      const auto nested = Hdn::RegenerateRemoteFace(&innerActor, innerActor.base.formID, innerMasks);
      assert(nested.result == Result::Done && nested.applyCount == 3);
      assert(Scope::Current() == state && state->snapshot == masks);
      ++state->applyCount;
    } else {
      assert(npc == &innerActor.base && state->snapshot == innerMasks);
      state->applyCount += 3;
    }
  };
  result = run();
  assert(result.result == Result::Done && result.applyCount == 2 && !Scope::Current());
  Test::onEngine = [&](auto, auto, auto) {
    ++Scope::Current()->applyCount;
    actor.base.formID++;
  };
  result = run();
  assert(result.result == Result::Drop && !Scope::Current());
  std::cout << "PASS regenerator: nested calls isolate masks/counts; base change during engine call drops completion\n";

  assert(Test::diagnosticLines.empty());
  {
    Scope scope(actor.formID, actor.base.formID, masks, 4, false);
    for (unsigned i = 0; i < 40; ++i) Hdn::LogRemoteFaceApply(*Scope::Current());
    for (unsigned i = 0; i < 40; ++i) Hdn::LogRemoteFaceRegen(actor.formID, actor.base.formID, 1, result);
  }
  unsigned applies = 0, completions = 0;
  for (const auto& line : Test::diagnosticLines) {
    assert(line.starts_with("[HDN][caras-dev-v3]"));
    applies += line.find("stage=apply") != std::string::npos;
    completions += line.find("stage=regen") != std::string::npos;
  }
  assert(Test::diagnosticLines.size() == 16 && applies == 8 && completions == 8);
  std::cout << "PASS diagnostics: 16 total records, eight reserved for each stage, exact DEV-v3 prefix\n";
}
