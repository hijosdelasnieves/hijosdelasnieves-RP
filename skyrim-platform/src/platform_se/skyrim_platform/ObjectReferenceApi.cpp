#include "ObjectReferenceApi.h"

#include "CallNativeApi.h"
#include "NullPointerException.h"

extern CallNativeApi::NativeCallRequirements g_nativeCallRequirements;

namespace {
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
