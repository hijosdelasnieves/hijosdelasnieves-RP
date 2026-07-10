#include "ObjectReferenceApi.h"

#include "CallNativeApi.h"
#include "NullPointerException.h"

#include <cmath>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

extern CallNativeApi::NativeCallRequirements g_nativeCallRequirements;

namespace {
struct ObjectReferenceTransform
{
  RE::NiPoint3 position;
  RE::NiPoint3 rotationRadians;
};

std::mutex g_transformMutex;
std::unordered_map<RE::FormID, ObjectReferenceTransform> g_pendingTransforms;
std::unordered_set<RE::FormID> g_scheduledTransforms;

RE::TESObjectREFR* GetArgObjectReference(const Napi::Value& arg)
{
  auto formId = NapiHelper::ExtractUInt32(arg, "refrFormId");
  auto refr = RE::TESForm::LookupByID<RE::TESObjectREFR>(formId);

  if (!refr) {
    throw NullPointerException("refr");
  }

  return refr;
}

void QueueCharacterControllerCollision(RE::FormID formId, bool enabled)
{
  g_nativeCallRequirements.gameThrQ->AddTask(
    [formId, enabled](Viet::Void) {
      auto actor = RE::TESForm::LookupByID<RE::Actor>(formId);
      auto controller = actor ? actor->GetCharController() : nullptr;
      if (!controller) {
        return;
      }

      if (enabled) {
        controller->flags.reset(
          RE::CHARACTER_FLAGS::kNoCharacterCollisions);
      } else {
        controller->flags.set(
          RE::CHARACTER_FLAGS::kNoCharacterCollisions);
      }
    });
}

void QueueObjectReferenceTransform(
  RE::FormID formId, const ObjectReferenceTransform& transform)
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
    const bool hadNoCharacterCollisions = controllerBefore &&
      controllerBefore->flags.any(
        RE::CHARACTER_FLAGS::kNoCharacterCollisions);

    // TESObjectREFR stores radians. Setting the rotation before SetPosition
    // makes CommonLib's MoveTo_Impl apply both parts as one game-thread update.
    refr->data.angle = latest.rotationRadians;
    refr->SetPosition(latest.position);

    // MoveTo_Impl is allowed to rebuild the live character controller. Preserve
    // the bilateral mounted-pair filter across that rebuild in the same task.
    if (hadNoCharacterCollisions && actor) {
      const auto controllerAfter = actor->GetCharController();
      if (controllerAfter) {
        controllerAfter->flags.set(
          RE::CHARACTER_FLAGS::kNoCharacterCollisions);
      }
    }
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
  auto refr = GetArgObjectReference(info[0]);
  const auto enabled =
    NapiHelper::ExtractBoolean(info[1], "collision");
  const auto formId = refr->GetFormID();

  // Keep the persistent reference flag used when a 3D is created. Actors
  // already loaded also have a live bhkCharacterController, whose collision
  // state is independent from that flag, so update it on the game thread.
  refr->SetCollision(enabled);
  QueueCharacterControllerCollision(formId, enabled);
  return info.Env().Undefined();
}

Napi::Value ObjectReferenceApi::SetCharacterControllerCollision(
  const Napi::CallbackInfo& info)
{
  const auto refr = GetArgObjectReference(info[0]);
  const auto enabled =
    NapiHelper::ExtractBoolean(info[1], "collision");
  QueueCharacterControllerCollision(refr->GetFormID(), enabled);
  return info.Env().Undefined();
}

Napi::Value ObjectReferenceApi::SetObjectReferenceTransform(
  const Napi::CallbackInfo& info)
{
  const auto refr = GetArgObjectReference(info[0]);
  constexpr float kDegreesToRadians =
    3.14159265358979323846f / 180.0f;
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
  QueueObjectReferenceTransform(refr->GetFormID(), transform);
  return info.Env().Undefined();
}
