#include "MockCpuComponents.h"
#include "RemoteFaceRegen.h"
#include "TintMask.h"
#include <iostream>
#include <mutex>

using IVM = void;
using StackID = std::uint32_t;
namespace TESModPlatform {
std::shared_ptr<RE::BSTArray<RE::TintMask*>> GetTintsFor(std::uint32_t);
void ClearTintMasks(IVM*, StackID, RE::StaticFunctionTag*, RE::Actor*);
void ClearRemoteFaceState();
}
#include "candidate-cpu.inc"

int main()
{
  using namespace TESModPlatform;
  auto actor = std::make_shared<RE::Actor>();
  auto push = [&](RE::BSFixedString path) {
    PushOwnedFaceTint(actor.get(), 6, 0x80402010, path);
  };
  auto assertNoLiveMemory = [] {
    assert(Test::allocations.empty());
    assert(Test::maskAllocations == Test::maskFrees);
    assert(Test::textureAllocations == Test::textureFrees);
    assert(Test::textureConstructions == Test::textureDestructions);
  };

  RE::BSFixedString path("textures/face/a.dds");
  auto stringLifetime = path.Weak();
  push(path);
  assert(Test::textureConstructorCalls == 1 && Test::textureInitializations == 1);
  assert(Test::textureConstructions == 1 && Test::textureDestructions == 0);
  auto snapshot = share2.faceOwners.at(actor->formID).snapshot;
  auto alias = GetTintsFor(actor->formID);
  const auto* raw = reinterpret_cast<::TintMask*>(snapshot->masks[0]);
  assert(raw->texture && raw->tintType == 6 && raw->color.red == 0x40);
  assert(raw->color.green == 0x20 && raw->color.blue == 0x10 && raw->color.alpha == 0x80);
  assert(raw->alpha == 128.f / 255.f);
  assert(std::string(raw->texture->textureName.c_str()) == "textures/face/a.dds");
  std::function<void()> callback = [held = snapshot] { assert(!held->masks.empty()); };
  path = RE::BSFixedString{};
  {
    Hdn::RemoteFaceTintScope active(actor->formID, actor->base.formID, snapshot, 4, false);
    ClearTintMasks(nullptr, 0, nullptr, actor.get());
    snapshot.reset(); alias.reset(); callback = {};
    assert(!stringLifetime.expired() && Test::textureDestructions == 0);
    assert(Test::maskFrees == 0 && Test::textureFrees == 0);
  }
  assert(stringLifetime.expired() && Test::liveStrings == 0);
  assert(Test::textureDestructions == 1 && Test::maskFrees == 1 && Test::textureFrees == 1);
  assertNoLiveMemory();
  std::cout << "PASS CPU: ctor14953/Initialize once; aliases/callback/scope retain texture, mask and string until last owner\n";

  push(RE::BSFixedString("textures/face/a.dds"));
  auto earlierSnapshot = share2.faceOwners.at(actor->formID).snapshot;
  const auto destroyedBeforeAppend = Test::textureDestructions;
  push(RE::BSFixedString("textures/face/b.dds"));
  auto appendedSnapshot = share2.faceOwners.at(actor->formID).snapshot;
  assert(earlierSnapshot->masks.size() == 1 && appendedSnapshot->masks.size() == 2);
  ClearRemoteFaceState();
  appendedSnapshot.reset();
  assert(Test::textureDestructions == destroyedBeforeAppend + 1);
  assert(Test::liveStrings == 1);
  earlierSnapshot.reset();
  assert(Test::textureDestructions == destroyedBeforeAppend + 2 && Test::liveStrings == 0);
  assertNoLiveMemory();
  std::cout << "PASS CPU: appending snapshots shares existing component owners without double destruction\n";

  push(RE::BSFixedString("textures/face/old.dds"));
  earlierSnapshot = share2.faceOwners.at(actor->formID).snapshot;
  const auto reusedId = actor->formID, reusedBase = actor->base.formID;
  actor.reset(); actor = std::make_shared<RE::Actor>(reusedId, reusedBase);
  push(RE::BSFixedString("textures/face/new.dds"));
  assert(share2.faceOwners.at(actor->formID).snapshot->masks.size() == 1);
  ClearTintMasks(nullptr, 0, nullptr, actor.get());
  assert(Test::liveStrings == 1);
  earlierSnapshot.reset(); assertNoLiveMemory(); assert(Test::liveStrings == 0);
  std::cout << "PASS CPU: reused actor generation owns a fresh snapshot and inherits no old components\n";

  Test::failMaskAllocation = true;
  assert(!CreateOwnedFaceTint(6, 0x80402010, RE::BSFixedString("oom-mask")));
  Test::failMaskAllocation = false; assertNoLiveMemory();
  Test::failTextureAllocation = true;
  assert(!CreateOwnedFaceTint(6, 0x80402010, RE::BSFixedString("oom-texture")));
  Test::failTextureAllocation = false; assertNoLiveMemory();
  Test::throwTextureConstructor = true;
  try {
    (void)CreateOwnedFaceTint(6, 0x80402010, RE::BSFixedString("throw-constructor")); assert(false);
  } catch (const std::runtime_error&) {}
  Test::throwTextureConstructor = false; assertNoLiveMemory();
  Test::throwTextureInitialization = true;
  try {
    (void)CreateOwnedFaceTint(6, 0x80402010, RE::BSFixedString("throw-initialize")); assert(false);
  } catch (const std::runtime_error&) {}
  Test::throwTextureInitialization = false; assertNoLiveMemory();
  assert(Test::liveStrings == 0);
  std::cout << "PASS CPU: mask/texture OOM and constructor/initializer exceptions release all acquired allocations\n";
  std::cout << "expected_mask_ctor_dtor=" << Test::maskAllocations << '\n';
  std::cout << "texture_ctor=" << Test::textureConstructions << " texture_dtor=" << Test::textureDestructions
            << " mask_alloc=" << Test::maskAllocations << " mask_free=" << Test::maskFrees
            << " texture_alloc=" << Test::textureAllocations << " texture_free=" << Test::textureFrees << '\n';
}
