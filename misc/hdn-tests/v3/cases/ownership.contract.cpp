#include "MockEngine.h"
#include "RemoteFaceRegen.h"
#include <atomic>
#include <iostream>
#include <mutex>
#include <set>
#include <unordered_map>

using IVM = void;
using StackID = std::uint32_t;
namespace TESModPlatform {
bool vmCallAllowed = false;
std::shared_ptr<RE::BSTArray<RE::TintMask*>> GetTintsFor(std::uint32_t);
void QueueRemoteFaceTintRefresh(RE::Actor*);
void UpdateRemoteFaceTints();
void ClearTintMasks(IVM*, StackID, RE::StaticFunctionTag*, RE::Actor*);
void ClearRemoteFaceState();
RE::BSEventNotifyControl FireLoadEvent();
RE::BSEventNotifyControl FireDeleteEvent(const RE::TESFormDeleteEvent*);
}

// Generated directly from the candidate worktree; the tests author no owner,
// cancellation, load-event or integration implementation.
#include "candidate-integration.inc"

namespace {
constexpr auto firstId = std::uint32_t{0xff000010};
RE::TintMask firstMask{11}, secondMask{22};
RE::BSFaceGenManager manager;

void Reset()
{
  auto& s = TESModPlatform::share2;
  Test::pendingTasks.clear();
  s.actorsTints.clear(); s.faceOwners.clear(); s.faceQueue = Hdn::RemoteFaceRegenQueue{};
  Test::handleLookups = Test::engineCalls = Test::engineCallsThisUpdate = 0;
  Test::regenerated.clear();
  Test::onEngine = [](auto, auto, auto) {
    assert(Hdn::RemoteFaceTintScope::Current());
    ++Hdn::RemoteFaceTintScope::Current()->applyCount;
  };
  Test::tasksAvailable = true; Test::throwOnEnqueue = false;
  RE::BSFaceGenManager::singleton = &manager;
  RE::PlayerCharacter::GetSingleton()->local.clear();
}

std::weak_ptr<RE::BSTArray<RE::TintMask*>> Snapshot(std::uint32_t id, RE::TintMask* mask)
{
  auto& s = TESModPlatform::share2;
  const auto index = id - 0xff000000;
  if (s.actorsTints.size() <= index) s.actorsTints.resize(index + 1);
  auto snapshot = std::make_shared<RE::BSTArray<RE::TintMask*>>();
  snapshot->push_back(mask); s.actorsTints[index] = snapshot;
  return snapshot;
}

std::weak_ptr<const Hdn::RemoteFaceTintSnapshot> Register(
  const std::shared_ptr<RE::Actor>& actor, RE::TintMask* mask = &firstMask)
{
  auto& s = TESModPlatform::share2;
  auto snapshot = std::make_shared<Hdn::RemoteFaceTintSnapshot>();
  snapshot->masks.push_back(mask);
  s.faceOwners[actor->formID] = { RE::ActorHandle(actor.get()), actor->base.formID, snapshot };
  assert(s.faceQueue.Request(actor->formID, actor->base.formID));
  return snapshot;
}

void RunPending()
{
  if (Test::pendingTasks.empty()) return;
  auto task = std::move(Test::pendingTasks.front());
  Test::pendingTasks.pop_front();
  assert(!Hdn::RemoteFaceTintScope::Current());
  task();
  assert(!Hdn::RemoteFaceTintScope::Current());
}

void Tick(unsigned number = 1, bool executeSKSE = true)
{
  while (number--) {
    Test::engineCallsThisUpdate = 0;
    TESModPlatform::UpdateRemoteFaceTints();
    assert(!Hdn::RemoteFaceTintScope::Current());
    if (executeSKSE) RunPending();
    assert(Test::engineCallsThisUpdate <= 1);
  }
}

void AssertRetired(std::uint32_t id)
{
  auto& s = TESModPlatform::share2;
  const auto index = id - 0xff000000;
  assert(index >= s.actorsTints.size() || !s.actorsTints[index]);
  assert(!s.faceOwners.contains(id));
  assert(s.faceQueue.Empty());
}
}

int main()
{
  using namespace TESModPlatform;

  Reset();
  auto original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original);
  assert(GetTintsFor(firstId)->front()->token == 11);
  original.reset();
  auto replacement = std::make_shared<RE::Actor>(firstId, 0xff000400);
  QueueRemoteFaceTintRefresh(replacement.get());
  assert(!GetTintsFor(firstId)); AssertRetired(firstId);
  Tick(310); assert(Test::engineCalls == 0);
  std::cout << "PASS automatic lookup: expired generation and reused ref/base inherit no tint\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original); original.reset();
  replacement = std::make_shared<RE::Actor>(firstId, 0xff000400);
  QueueRemoteFaceTintRefresh(replacement.get()); Tick(4);
  assert(Test::engineCalls == 0 && share2.faceQueue.Empty());
  assert(!GetTintsFor(firstId)); AssertRetired(firstId);
  std::cout << "PASS explicit refresh: reused FormID never resolves a replacement generation\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original); original->base.formID = 0xff000401;
  QueueRemoteFaceTintRefresh(original.get()); Tick(4);
  assert(Test::engineCalls == 0 && share2.faceQueue.Empty());
  assert(!GetTintsFor(firstId)); AssertRetired(firstId);
  std::cout << "PASS changed base: job and stale snapshot retire before engine use\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original); ClearTintMasks(nullptr, 0, nullptr, original.get());
  AssertRetired(firstId); Tick(310); assert(Test::engineCalls == 0);
  Register(original);
  RE::PlayerCharacter::GetSingleton()->local.push_back(&secondMask);
  ClearTintMasks(nullptr, 0, nullptr, nullptr);
  assert(RE::PlayerCharacter::GetSingleton()->local.empty());
  assert(GetTintsFor(firstId));
  std::cout << "PASS Clear: exact remote cancellation; local clear retains remote masks\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original);
  const auto legacyId = firstId + 1;
  auto legacySnapshot = Snapshot(legacyId, &secondMask).lock();
  assert(FireLoadEvent() == RE::BSEventNotifyControl::kContinue);
  assert(vmCallAllowed && share2.faceOwners.empty() && share2.faceQueue.Empty());
  const auto ownedIndex = firstId - 0xff000000;
  assert(ownedIndex >= share2.actorsTints.size() || !share2.actorsTints[ownedIndex]);
  assert(share2.actorsTints[legacyId - 0xff000000] == legacySnapshot);
  assert(GetTintsFor(legacyId)->front()->token == 22);
  Tick(310); assert(Test::engineCalls == 0);
  std::cout << "PASS load: owned snapshots/jobs cancel while legacy data remains intact\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  auto pending = Register(original);
  Test::onEngine = [&](auto, auto, auto) {
    ClearTintMasks(nullptr, 0, nullptr, original.get());
    assert(!pending.expired());
  };
  Tick(3);
  assert(Test::engineCalls == 1 && pending.expired()); AssertRetired(firstId);
  std::cout << "PASS reentrant Clear: active snapshot survives engine call and then releases\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  auto oldSnapshot = Register(original);
  Tick(3, false); assert(Test::pendingTasks.size() == 1 && Test::engineCalls == 0);
  ClearTintMasks(nullptr, 0, nullptr, original.get());
  assert(!oldSnapshot.expired());
  auto newSnapshot = Register(original, &secondMask);
  Tick(10, false);
  assert(Test::pendingTasks.size() == 1 && Test::engineCalls == 0);
  RunPending();
  assert(Test::engineCalls == 0 && oldSnapshot.expired() && !newSnapshot.expired());
  Test::onEngine = [&](auto, auto, auto) {
    auto* scope = Hdn::RemoteFaceTintScope::Current();
    assert(scope && scope->snapshot == newSnapshot.lock());
    assert(scope->snapshot->masks[0]->token == 22);
    ++scope->applyCount;
  };
  Tick(); assert(Test::engineCalls == 1 && share2.faceQueue.Empty());
  std::cout << "PASS queued Clear/re-register: old callback cannot use, free or retire replacement masks\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  oldSnapshot = Register(original); Tick(3, false);
  const auto oldEpoch = share2.faceEpoch;
  FireLoadEvent(); assert(share2.faceEpoch != oldEpoch);
  newSnapshot = Register(original, &secondMask); Tick(5, false);
  assert(Test::pendingTasks.size() == 1);
  RunPending(); assert(Test::engineCalls == 0 && oldSnapshot.expired());
  Tick(); assert(Test::engineCalls == 1 && share2.faceQueue.Empty() && !newSnapshot.expired());
  std::cout << "PASS queued load: epoch invalidates old callback and preserves newer appearance\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  oldSnapshot = Register(original); Tick(3, false);
  const RE::TESFormDeleteEvent deleting{firstId};
  const RE::TESFormDeleteEvent localDeletion{0x14};
  FireDeleteEvent(nullptr); FireDeleteEvent(&localDeletion);
  assert(share2.faceOwners.contains(firstId));
  FireDeleteEvent(&deleting);
  assert(!share2.faceOwners.contains(firstId) && !oldSnapshot.expired());
  assert(share2.faceQueue.InFlight() && Test::pendingTasks.size() == 1);
  RunPending(); assert(Test::engineCalls == 0 && oldSnapshot.expired() && share2.faceQueue.Empty());
  oldSnapshot = Register(original); Tick(3);
  assert(Test::engineCalls == 1 && !oldSnapshot.expired());
  FireDeleteEvent(&deleting);
  assert(oldSnapshot.expired() && !share2.faceOwners.contains(firstId));
  std::cout << "PASS delete event: pending callback retains cancelled snapshot until drain; finished owner releases immediately\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  oldSnapshot = Register(original);
  Test::onEngine = [&](auto, auto, auto) {
    FireDeleteEvent(&deleting);
    assert(!oldSnapshot.expired());
    assert(Hdn::RemoteFaceTintScope::Current()->snapshot == oldSnapshot.lock());
  };
  Tick(3);
  assert(Test::engineCalls == 1 && oldSnapshot.expired() && !share2.faceOwners.contains(firstId));
  assert(share2.faceQueue.Empty());
  std::cout << "PASS delete during engine call: active scope retains masks through completion\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original); Tick(3, false); original.reset();
  replacement = std::make_shared<RE::Actor>(firstId, 0xff000400);
  RunPending(); assert(Test::engineCalls == 0 && share2.faceQueue.Empty());
  assert(!GetTintsFor(firstId));
  std::cout << "PASS handle expires after enqueue: callback rejects reused actor generation\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original); Tick(3, false); original->base.formID++;
  RunPending(); assert(Test::engineCalls == 0 && share2.faceQueue.Empty());
  assert(!GetTintsFor(firstId));
  std::cout << "PASS base changes after enqueue: callback validates again before entering engine\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original);
  Test::onEngine = [&](auto, auto, auto) {
    QueueRemoteFaceTintRefresh(original.get());
    // No ApplyMasks callback: telemetry must report zero, without retrying.
  };
  Tick(100);
  assert(Test::engineCalls == 1 && share2.faceQueue.Empty());
  std::cout << "PASS zero apply and engine NiNode callback: exactly one regeneration with no automatic requeue\n";

  Reset();
  std::vector<std::shared_ptr<RE::Actor>> population;
  std::set<std::uint32_t> regeneratedBases;
  Test::onEngine = [&](auto, auto, auto* base) {
    assert(regeneratedBases.insert(base->GetFormID()).second);
  };
  for (std::uint32_t i = 0; i < 256; ++i) {
    auto actor = std::make_shared<RE::Actor>(firstId + i, 0xff000400 + i);
    Register(actor); population.push_back(std::move(actor));
  }
  unsigned ticks = 0;
  while (!share2.faceQueue.Empty() && ticks < 300) { Tick(); ++ticks; }
  assert(share2.faceQueue.Empty() && Test::engineCalls == 256 && ticks < 300);
  assert(regeneratedBases.size() == 256);
  const auto resolves = Test::handleLookups;
  Tick(1000);
  assert(Test::engineCalls == 256 && Test::handleLookups == resolves);
  std::cout << "PASS integration scale: 256 actors, <=1 engine call/update, " << ticks
            << " updates (<300); 1000 idle updates cause no lookup/engine call\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  original->loaded = false; Register(original);
  Tick(12); assert(Test::engineCalls == 0 && share2.faceQueue.Empty());
  std::cout << "PASS integration readiness: unloaded actor exhausts four bounded attempts without engine use\n";

  Reset(); original = std::make_shared<RE::Actor>(firstId, 0xff000400);
  Register(original); Test::tasksAvailable = false; Tick(3);
  assert(Test::engineCalls == 0 && share2.faceQueue.Empty() && Test::pendingTasks.empty());
  Register(original); Test::tasksAvailable = true; Test::throwOnEnqueue = true; Tick(3);
  assert(Test::engineCalls == 0 && share2.faceQueue.Empty() && Test::pendingTasks.empty());
  std::cout << "PASS missing/throwing SKSE task interface retires ticket without orphaning reservation\n";
}
