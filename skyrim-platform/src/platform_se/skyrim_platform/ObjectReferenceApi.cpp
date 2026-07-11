#include "ObjectReferenceApi.h"

#include "CallNativeApi.h"

#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

extern CallNativeApi::NativeCallRequirements g_nativeCallRequirements;

namespace {
enum class CharacterControllerCollisionProfile : std::uint8_t
{
  kNone = 0,
  kMountedHorse = 1,
  kRemoteProxy = 2,
};

struct CharacterControllerCollisionOverride
{
  CharacterControllerCollisionProfile profile =
    CharacterControllerCollisionProfile::kNone;
  std::uint32_t lease = 0;
  RE::ActorHandle actorHandle;
  RE::NiPointer<RE::bhkCharacterController> boundController;
  bool hasControllerSnapshot = false;
  bool ownedNoCharacterCollisions = false;
  bool ownedNotPushable = false;
  bool ownedNotPushablePermanent = false;
  bool ownedPossiblePathObstacleReset = false;
  bool pathObstacleOverrideApplied = false;
  bool missingControllerLogged = false;
  std::uint32_t bindGeneration = 0;
};

struct ObjectReferenceTransform
{
  RE::NiPoint3 position;
  RE::NiPoint3 rotationRadians;
};

std::mutex g_transformMutex;
std::unordered_map<RE::FormID, ObjectReferenceTransform> g_pendingTransforms;
std::unordered_set<RE::FormID> g_scheduledTransforms;

// Profile mutations and maintenance run exclusively on the game thread. The
// small status cache is the only cross-thread state: JavaScript reads it for
// diagnostics while the game thread publishes controller flags.
std::unordered_map<RE::FormID, CharacterControllerCollisionOverride>
  g_characterControllerCollisionOverrides;
std::mutex g_characterControllerCollisionStateMutex;
std::unordered_map<RE::FormID, std::uint32_t>
  g_characterControllerCollisionState;

constexpr std::uint32_t kProfileStateRegistered = 1u << 0;
constexpr std::uint32_t kProfileStateControllerPresent = 1u << 1;
constexpr std::uint32_t kProfileStateNoCharacterCollisions = 1u << 2;
constexpr std::uint32_t kProfileStateNotPushablePermanent = 1u << 3;
constexpr std::uint32_t kProfileStatePossiblePathObstacle = 1u << 4;
constexpr std::uint32_t kProfileStateNotPushable = 1u << 5;

void ApplyCharacterControllerCollision(RE::FormID formId, bool enabled)
{
  const auto actor = RE::TESForm::LookupByID<RE::Actor>(formId);
  const auto controller = actor ? actor->GetCharController() : nullptr;
  if (!controller) {
    return;
  }

  if (enabled) {
    controller->flags.reset(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
  } else {
    controller->flags.set(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
  }
}

void QueueCharacterControllerCollision(RE::FormID formId, bool enabled)
{
  g_nativeCallRequirements.gameThrQ->AddTask([formId, enabled](Viet::Void) {
    ApplyCharacterControllerCollision(formId, enabled);
  });
}

void QueueObjectReferenceCollision(RE::FormID formId, bool enabled)
{
  g_nativeCallRequirements.gameThrQ->AddTask([formId, enabled](Viet::Void) {
    const auto refr = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId);
    if (refr) {
      refr->SetCollision(enabled);
    }
    ApplyCharacterControllerCollision(formId, enabled);
  });
}

void PublishCharacterControllerCollisionState(
  RE::FormID formId, const CharacterControllerCollisionOverride& state,
  RE::bhkCharacterController* controller)
{
  auto value = kProfileStateRegistered |
    (static_cast<std::uint32_t>(state.profile) << 8) |
    ((state.bindGeneration & 0xffffu) << 16);
  if (controller) {
    value |= kProfileStateControllerPresent;
    if (controller->flags.any(RE::CHARACTER_FLAGS::kNoCharacterCollisions)) {
      value |= kProfileStateNoCharacterCollisions;
    }
    if (controller->flags.any(RE::CHARACTER_FLAGS::kNotPushablePermanent)) {
      value |= kProfileStateNotPushablePermanent;
    }
    if (controller->flags.any(RE::CHARACTER_FLAGS::kPossiblePathObstacle)) {
      value |= kProfileStatePossiblePathObstacle;
    }
    if (controller->flags.any(RE::CHARACTER_FLAGS::kNotPushable)) {
      value |= kProfileStateNotPushable;
    }
  }

  std::lock_guard lock(g_characterControllerCollisionStateMutex);
  g_characterControllerCollisionState[formId] = value;
}

void EraseCharacterControllerCollisionState(RE::FormID formId)
{
  std::lock_guard lock(g_characterControllerCollisionStateMutex);
  g_characterControllerCollisionState.erase(formId);
}

void RestoreOwnedCharacterControllerFlags(
  CharacterControllerCollisionOverride& state)
{
  const auto controller = state.boundController.get();
  if (controller && state.hasControllerSnapshot) {
    if (state.ownedNoCharacterCollisions) {
      controller->flags.reset(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
    }
    if (state.ownedNotPushable) {
      controller->flags.reset(RE::CHARACTER_FLAGS::kNotPushable);
    }
    if (state.ownedNotPushablePermanent) {
      controller->flags.reset(RE::CHARACTER_FLAGS::kNotPushablePermanent);
    }
    if (state.pathObstacleOverrideApplied &&
        state.ownedPossiblePathObstacleReset) {
      controller->flags.set(RE::CHARACTER_FLAGS::kPossiblePathObstacle);
    }
  }

  state.boundController.reset();
  state.hasControllerSnapshot = false;
  state.ownedNoCharacterCollisions = false;
  state.ownedNotPushable = false;
  state.ownedNotPushablePermanent = false;
  state.ownedPossiblePathObstacleReset = false;
  state.pathObstacleOverrideApplied = false;
}

void BindCharacterController(RE::FormID formId,
                             CharacterControllerCollisionOverride& state,
                             RE::bhkCharacterController* controller)
{
  RestoreOwnedCharacterControllerFlags(state);
  state.boundController.reset(controller);
  state.hasControllerSnapshot = true;
  state.ownedNoCharacterCollisions =
    !controller->flags.any(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
  state.ownedNotPushable =
    !controller->flags.any(RE::CHARACTER_FLAGS::kNotPushable);
  state.ownedNotPushablePermanent =
    !controller->flags.any(RE::CHARACTER_FLAGS::kNotPushablePermanent);
  state.missingControllerLogged = false;
  ++state.bindGeneration;

  logger::info("Character controller collision profile bound: form={:08X}, "
               "profile={}, generation={}, controller={}",
               formId, static_cast<std::uint32_t>(state.profile),
               state.bindGeneration, static_cast<const void*>(controller));
}

bool ApplyCharacterControllerCollisionOverride(
  RE::FormID formId, CharacterControllerCollisionOverride& state)
{
  const auto actor = RE::TESForm::LookupByID<RE::Actor>(formId);
  if (!actor) {
    logger::info(
      "Dropping character controller collision profile with no actor: "
      "form={:08X}, profile={}",
      formId, static_cast<std::uint32_t>(state.profile));
    return false;
  }
  const auto controller = actor ? actor->GetCharController() : nullptr;
  const auto actorHandle = actor->GetHandle();
  if (!actorHandle || !state.actorHandle || state.actorHandle != actorHandle) {
    logger::warn(
      "Dropping stale character controller collision profile after form "
      "reuse: form={:08X}, oldHandle={}, newHandle={}",
      formId, state.actorHandle.native_handle(), actorHandle.native_handle());
    return false;
  }

  if (!controller) {
    // A detached middle-high process can stay unloaded for minutes. Release
    // the old NiPointer after restoring our owned bits; the FormID/profile
    // remains registered and will bind a fresh controller when 3D returns.
    RestoreOwnedCharacterControllerFlags(state);
    if (!state.missingControllerLogged) {
      logger::info(
        "Character controller collision profile waiting for controller: "
        "form={:08X}, profile={}",
        formId, static_cast<std::uint32_t>(state.profile));
      state.missingControllerLogged = true;
    }
    PublishCharacterControllerCollisionState(formId, state, nullptr);
    return true;
  }

  if (!state.hasControllerSnapshot ||
      state.boundController.get() != controller) {
    BindCharacterController(formId, state, controller);
  }

  controller->flags.set(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
  controller->flags.set(RE::CHARACTER_FLAGS::kNotPushable);
  controller->flags.set(RE::CHARACTER_FLAGS::kNotPushablePermanent);

  if (state.profile == CharacterControllerCollisionProfile::kRemoteProxy) {
    if (!state.pathObstacleOverrideApplied) {
      state.ownedPossiblePathObstacleReset =
        controller->flags.any(RE::CHARACTER_FLAGS::kPossiblePathObstacle);
    }
    controller->flags.reset(RE::CHARACTER_FLAGS::kPossiblePathObstacle);
    state.pathObstacleOverrideApplied = true;
  } else if (state.pathObstacleOverrideApplied) {
    if (state.ownedPossiblePathObstacleReset) {
      controller->flags.set(RE::CHARACTER_FLAGS::kPossiblePathObstacle);
    }
    state.ownedPossiblePathObstacleReset = false;
    state.pathObstacleOverrideApplied = false;
  }

  PublishCharacterControllerCollisionState(formId, state, controller);
  return true;
}

void SetCharacterControllerCollisionProfile(
  RE::FormID formId, CharacterControllerCollisionProfile profile,
  std::uint32_t lease)
{
  if (profile == CharacterControllerCollisionProfile::kNone) {
    const auto it = g_characterControllerCollisionOverrides.find(formId);
    if (it != g_characterControllerCollisionOverrides.end()) {
      if (lease != 0 && it->second.lease != 0 && lease != it->second.lease) {
        logger::info(
          "Ignoring stale character controller collision profile clear: "
          "form={:08X}, lease={}, activeLease={}",
          formId, lease, it->second.lease);
        return;
      }
      RestoreOwnedCharacterControllerFlags(it->second);
      g_characterControllerCollisionOverrides.erase(it);
    }
    EraseCharacterControllerCollisionState(formId);
    return;
  }

  const auto actor = RE::TESForm::LookupByID<RE::Actor>(formId);
  const auto actorHandle = actor ? actor->GetHandle() : RE::ActorHandle{};
  if (!actor || !actorHandle) {
    logger::warn(
      "Ignoring character controller collision profile for missing actor: "
      "form={:08X}, profile={}",
      formId, static_cast<std::uint32_t>(profile));
    return;
  }

  auto existing = g_characterControllerCollisionOverrides.find(formId);
  if (existing != g_characterControllerCollisionOverrides.end() && actor &&
      existing->second.actorHandle &&
      actorHandle != existing->second.actorHandle) {
    RestoreOwnedCharacterControllerFlags(existing->second);
    EraseCharacterControllerCollisionState(formId);
    g_characterControllerCollisionOverrides.erase(existing);
  }

  const auto it =
    g_characterControllerCollisionOverrides.try_emplace(formId).first;
  auto& state = it->second;
  state.actorHandle = actorHandle;
  state.profile = profile;
  state.lease = lease;
  if (!ApplyCharacterControllerCollisionOverride(formId, state)) {
    RestoreOwnedCharacterControllerFlags(state);
    EraseCharacterControllerCollisionState(formId);
    g_characterControllerCollisionOverrides.erase(it);
  }
}

void MaintainCharacterControllerCollisionOverride(RE::FormID formId)
{
  const auto it = g_characterControllerCollisionOverrides.find(formId);
  if (it != g_characterControllerCollisionOverrides.end()) {
    if (!ApplyCharacterControllerCollisionOverride(formId, it->second)) {
      RestoreOwnedCharacterControllerFlags(it->second);
      EraseCharacterControllerCollisionState(formId);
      g_characterControllerCollisionOverrides.erase(it);
    }
  }
}

void ClearCharacterControllerCollisionProfiles()
{
  for (auto& entry : g_characterControllerCollisionOverrides) {
    RestoreOwnedCharacterControllerFlags(entry.second);
  }
  g_characterControllerCollisionOverrides.clear();

  std::lock_guard lock(g_characterControllerCollisionStateMutex);
  g_characterControllerCollisionState.clear();
}

void QueueObjectReferenceTransform(RE::FormID formId,
                                   const ObjectReferenceTransform& transform)
{
  bool mustSchedule = false;
  {
    std::lock_guard lock(g_transformMutex);
    g_pendingTransforms[formId] = transform;
    mustSchedule = g_scheduledTransforms.insert(formId).second;
  }
  if (!mustSchedule) {
    return;
  }

  g_nativeCallRequirements.gameThrQ->AddTask([formId](Viet::Void) {
    ObjectReferenceTransform latest;
    {
      std::lock_guard lock(g_transformMutex);
      const auto it = g_pendingTransforms.find(formId);
      if (it == g_pendingTransforms.end()) {
        g_scheduledTransforms.erase(formId);
        return;
      }
      latest = it->second;
      g_pendingTransforms.erase(it);
      g_scheduledTransforms.erase(formId);
    }

    const auto refr = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId);
    if (!refr) {
      return;
    }

    const auto actor = RE::TESForm::LookupByID<RE::Actor>(formId);
    const auto controllerBefore = actor ? actor->GetCharController() : nullptr;
    const bool hasCollisionProfile =
      g_characterControllerCollisionOverrides.contains(formId);
    const bool hadNoCharacterCollisions = !hasCollisionProfile &&
      controllerBefore &&
      controllerBefore->flags.any(RE::CHARACTER_FLAGS::kNoCharacterCollisions);

    // TESObjectREFR stores radians. Setting the rotation before SetPosition
    // makes CommonLib's MoveTo_Impl apply both parts as one game-thread
    // update.
    refr->data.angle = latest.rotationRadians;
    refr->SetPosition(latest.position);

    // MoveTo_Impl is allowed to rebuild the live character controller.
    // Preserve the bilateral mounted-pair filter across that rebuild in the
    // same task.
    if (hadNoCharacterCollisions && actor) {
      const auto controllerAfter = actor->GetCharController();
      if (controllerAfter) {
        controllerAfter->flags.set(
          RE::CHARACTER_FLAGS::kNoCharacterCollisions);
      }
    }

    // A position write can replace the live controller. Rebind every managed
    // profile before the next physics step, including its no-push/path flags.
    MaintainCharacterControllerCollisionOverride(formId);
  });
}

float ExtractFiniteFloat(const Napi::Value& value, const char* name)
{
  const auto result = NapiHelper::ExtractFloat(value, name);
  if (!std::isfinite(result)) {
    throw std::runtime_error(std::string("Expected '") + name +
                             "' to be finite");
  }
  return result;
}
}

Napi::Value ObjectReferenceApi::SetCollision(const Napi::CallbackInfo& info)
{
  const auto formId = NapiHelper::ExtractUInt32(info[0], "refrFormId");
  const auto enabled = NapiHelper::ExtractBoolean(info[1], "collision");

  // Keep the persistent reference flag used when a 3D is created. Actors
  // already loaded also have a live bhkCharacterController, whose collision
  // state is independent from that flag. Both writes must run on the game
  // thread; JavaScript executes on SkyrimPlatform's worker.
  QueueObjectReferenceCollision(formId, enabled);
  return info.Env().Undefined();
}

Napi::Value ObjectReferenceApi::SetCharacterControllerCollision(
  const Napi::CallbackInfo& info)
{
  const auto formId = NapiHelper::ExtractUInt32(info[0], "actorFormId");
  const auto enabled = NapiHelper::ExtractBoolean(info[1], "collision");
  QueueCharacterControllerCollision(formId, enabled);
  return info.Env().Undefined();
}

Napi::Value ObjectReferenceApi::SetCharacterControllerCollisionProfile(
  const Napi::CallbackInfo& info)
{
  const auto formId = NapiHelper::ExtractUInt32(info[0], "actorFormId");
  const auto rawProfile = NapiHelper::ExtractUInt32(info[1], "profile");
  if (rawProfile > static_cast<std::uint32_t>(
                     CharacterControllerCollisionProfile::kRemoteProxy)) {
    throw std::runtime_error("Expected 'profile' to be 0, 1, or 2");
  }

  const auto profile =
    static_cast<CharacterControllerCollisionProfile>(rawProfile);
  const auto lease =
    info.Length() >= 3 && !info[2].IsUndefined() && !info[2].IsNull()
    ? NapiHelper::ExtractUInt32(info[2], "lease")
    : 0;
  g_nativeCallRequirements.gameThrQ->AddTask(
    [formId, profile, lease](Viet::Void) {
      SetCharacterControllerCollisionProfile(formId, profile, lease);
    });
  return info.Env().Undefined();
}

Napi::Value ObjectReferenceApi::GetCharacterControllerCollisionProfileState(
  const Napi::CallbackInfo& info)
{
  const auto formId = NapiHelper::ExtractUInt32(info[0], "actorFormId");
  std::lock_guard lock(g_characterControllerCollisionStateMutex);
  const auto it = g_characterControllerCollisionState.find(formId);
  return Napi::Number::New(
    info.Env(),
    it == g_characterControllerCollisionState.end() ? 0 : it->second);
}

Napi::Value ObjectReferenceApi::SetObjectReferenceTransform(
  const Napi::CallbackInfo& info)
{
  const auto formId = NapiHelper::ExtractUInt32(info[0], "refrFormId");
  constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
  const ObjectReferenceTransform transform{
    .position = {
      ExtractFiniteFloat(info[1], "positionX"),
      ExtractFiniteFloat(info[2], "positionY"),
      ExtractFiniteFloat(info[3], "positionZ"),
    },
    .rotationRadians = {
      ExtractFiniteFloat(info[4], "angleX") * kDegreesToRadians,
      ExtractFiniteFloat(info[5], "angleY") * kDegreesToRadians,
      ExtractFiniteFloat(info[6], "angleZ") * kDegreesToRadians,
    },
  };
  QueueObjectReferenceTransform(formId, transform);
  return info.Env().Undefined();
}

void ObjectReferenceApi::MaintainCharacterControllerCollisionProfiles()
{
  auto it = g_characterControllerCollisionOverrides.begin();
  while (it != g_characterControllerCollisionOverrides.end()) {
    if (ApplyCharacterControllerCollisionOverride(it->first, it->second)) {
      ++it;
      continue;
    }
    RestoreOwnedCharacterControllerFlags(it->second);
    EraseCharacterControllerCollisionState(it->first);
    it = g_characterControllerCollisionOverrides.erase(it);
  }
}

void ObjectReferenceApi::ClearCharacterControllerCollisionProfiles()
{
  ::ClearCharacterControllerCollisionProfiles();
}

void ObjectReferenceApi::QueueClearCharacterControllerCollisionProfiles()
{
  g_nativeCallRequirements.gameThrQ->AddTask(
    [](Viet::Void) { ::ClearCharacterControllerCollisionProfiles(); });
}
