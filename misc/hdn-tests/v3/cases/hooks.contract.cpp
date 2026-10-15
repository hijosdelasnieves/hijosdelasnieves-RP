#include "MockEngine.h"
#include "RemoteFaceRegen.h"
#include <iostream>
#include <unordered_map>

struct GumCpuContext { std::uintptr_t rcx = 0; };
struct GumInvocationContext {
  void* invocationData = nullptr;
  GumCpuContext* cpu_context = nullptr;
  std::array<void*, 2> arguments{};
  unsigned replacements = 0;
};
#define GUM_IC_GET_INVOCATION_DATA(context, type) static_cast<type*>((context)->invocationData)
void gum_invocation_context_replace_nth_argument(GumInvocationContext* context, unsigned index, void* value)
{
  assert(index == 0);
  context->arguments[index] = value;
  ++context->replacements;
}
namespace Test {
inline unsigned lookups = 0, refreshes = 0;
inline bool forbidLookup = false;
inline std::unordered_map<std::uint32_t, std::shared_ptr<RE::BSTArray<RE::TintMask*>>> automatic;
}
namespace TESModPlatform {
std::shared_ptr<RE::BSTArray<RE::TintMask*>> GetTintsFor(std::uint32_t actor)
{
  assert(!Test::forbidLookup);
  ++Test::lookups;
  const auto found = Test::automatic.find(actor);
  return found == Test::automatic.end() ? nullptr : found->second;
}
void QueueRemoteFaceTintRefresh(RE::Actor* actor) { assert(actor); ++Test::refreshes; }
}

// These four hook bodies, invocation data and legacy TLS declaration are exact
// source slices; only Gum's argument/invocation-data API is mocked.
#include "candidate-hooks.inc"

int main()
{
  using Scope = Hdn::RemoteFaceTintScope;
  RE::TintMask first{11}, second{22};
  RE::BSTArray<RE::TintMask*> originalMasks{&second};
  ApplyMasksInvocationData applyData{};
  GumInvocationContext apply{&applyData, nullptr, {&originalMasks, nullptr}};
  OnApplyMasksToRenderTargetsEnter(&apply);
  assert(apply.arguments[0] == &originalMasks && apply.replacements == 0);
  assert(Test::lookups == 0); OnApplyMasksToRenderTargetsLeave(&apply);

  auto masks = std::make_shared<Hdn::RemoteFaceTintSnapshot>();
  masks->masks.push_back(&first);
  RE::Actor actor;
  RE::BSFaceGenManager manager;
  RE::BSFaceGenManager::singleton = &manager;
  Test::onEngine = [&](auto, auto, auto) {
    Test::forbidLookup = true;
    for (unsigned i = 0; i < 3; ++i) {
      apply.arguments[0] = &originalMasks;
      OnApplyMasksToRenderTargetsEnter(&apply);
      assert(apply.arguments[0] == &masks->masks && !applyData.heldTints);
      OnApplyMasksToRenderTargetsLeave(&apply);
    }
    // Scoped path also works without an invocation-data allocation.
    GumInvocationContext noInvocation{nullptr, nullptr, {&originalMasks, nullptr}};
    OnApplyMasksToRenderTargetsEnter(&noInvocation);
    assert(noInvocation.arguments[0] == &masks->masks);
    OnApplyMasksToRenderTargetsLeave(&noInvocation);
    Test::forbidLookup = false;
  };
  const auto result = Hdn::RegenerateRemoteFace(&actor, actor.base.formID, masks);
  assert(result.engineCalled && result.applyCount == 4 && !Scope::Current());
  assert(Test::lookups == 0);
  apply.arguments[0] = &originalMasks;
  OnApplyMasksToRenderTargetsEnter(&apply);
  assert(apply.arguments[0] == &originalMasks); OnApplyMasksToRenderTargetsLeave(&apply);
  std::cout << "PASS real regenerator+real ApplyMasks hook: four replacements during engine call, none outside, zero actor lookups\n";

  auto automatic = std::make_shared<RE::BSTArray<RE::TintMask*>>();
  automatic->push_back(&second);
  std::weak_ptr<RE::BSTArray<RE::TintMask*>> lifetime = automatic;
  Test::automatic[actor.formID] = automatic;
  g_queueNiNodeActorId = actor.formID;
  OnApplyMasksToRenderTargetsEnter(&apply);
  assert(apply.arguments[0] == automatic.get() && applyData.heldTints && g_queueNiNodeActorId == 0);
  Test::automatic.erase(actor.formID); automatic.reset();
  assert(!lifetime.expired());
  OnApplyMasksToRenderTargetsLeave(&apply);
  assert(lifetime.expired() && !applyData.heldTints);
  std::cout << "PASS automatic ApplyMasks hook: alias remains alive until matching leave callback\n";

  RE::Actor actorA(0xff000201, 0xff000901), actorB(0xff000202, 0xff000902);
  Test::automatic[actorA.formID] = std::make_shared<RE::BSTArray<RE::TintMask*>>(1, &first);
  Test::automatic[actorB.formID] = std::make_shared<RE::BSTArray<RE::TintMask*>>(1, &second);
  QueueNiNodeInvocationData dataA{}, dataB{};
  GumCpuContext cpuA{reinterpret_cast<std::uintptr_t>(static_cast<RE::TESObjectREFR*>(&actorA))};
  GumCpuContext cpuB{reinterpret_cast<std::uintptr_t>(static_cast<RE::TESObjectREFR*>(&actorB))};
  GumInvocationContext queueA{&dataA, &cpuA}, queueB{&dataB, &cpuB};
  OnQueueNinodeUpdateEnter(&queueA); assert(g_queueNiNodeActorId == actorA.formID);
  OnQueueNinodeUpdateEnter(&queueB); assert(g_queueNiNodeActorId == actorB.formID);
  OnApplyMasksToRenderTargetsEnter(&apply);
  assert(apply.arguments[0] == Test::automatic[actorB.formID].get());
  OnApplyMasksToRenderTargetsLeave(&apply);
  OnQueueNinodeUpdateLeave(&queueB); assert(g_queueNiNodeActorId == actorA.formID);
  OnApplyMasksToRenderTargetsEnter(&apply);
  assert(apply.arguments[0] == Test::automatic[actorA.formID].get());
  OnApplyMasksToRenderTargetsLeave(&apply);
  OnQueueNinodeUpdateLeave(&queueA); assert(g_queueNiNodeActorId == 0);
  assert(!dataA.active && !dataB.active && Test::refreshes == 2);
  std::cout << "PASS real NiNode hook: nested actor contexts restore parent and bind the correct automatic masks\n";

  RE::TESObjectREFR object;
  GumCpuContext objectCpu{reinterpret_cast<std::uintptr_t>(&object)};
  GumInvocationContext objectQueue{&dataA, &objectCpu};
  g_queueNiNodeActorId = actorA.formID;
  OnQueueNinodeUpdateEnter(&objectQueue); assert(g_queueNiNodeActorId == 0);
  apply.arguments[0] = &originalMasks;
  OnApplyMasksToRenderTargetsEnter(&apply);
  assert(apply.arguments[0] == &originalMasks);
  OnApplyMasksToRenderTargetsLeave(&apply);
  OnQueueNinodeUpdateLeave(&objectQueue); assert(g_queueNiNodeActorId == actorA.formID);
  g_queueNiNodeActorId = 0;
  std::cout << "PASS real NiNode hook: non-actor rebuild inherits no actor masks\n";

  {
    auto empty = std::make_shared<Hdn::RemoteFaceTintSnapshot>();
    Scope scope(actorA.formID, actorA.base.formID, empty, 4, false);
    g_queueNiNodeActorId = actorB.formID;
    apply.arguments[0] = &originalMasks;
    const auto lookups = Test::lookups;
    OnApplyMasksToRenderTargetsEnter(&apply);
    assert(apply.arguments[0] == &originalMasks && Test::lookups == lookups);
    OnApplyMasksToRenderTargetsLeave(&apply);
  }
  g_queueNiNodeActorId = 0;
  std::cout << "PASS scoped empty snapshot cannot fall through to another actor's automatic masks\n";
}
