#include "RemoteFaceRegen.h"
#include "ConsoleApi.h"

#include <RE/A/Actor.h>
#include <RE/B/BGSHeadPart.h>
#include <RE/B/BSFaceGenManager.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSLightingShaderMaterialFacegen.h>
#include <RE/B/BSLightingShaderProperty.h>
#include <RE/N/NiRTTI.h>
#include <REL/Relocation.h>
#include <atomic>

namespace Hdn {
namespace {
bool Dynamic(std::uint32_t id)
{
  return id >= 0xff000000 && id != 0xffffffff;
}

bool ExactActor(RE::Actor* actor, std::uint32_t expectedBase)
{
  if (!actor || !Dynamic(actor->GetFormID()) || !Dynamic(expectedBase) ||
      actor->IsDeleted() || actor->IsMarkedForDeletion())
    return false;
  const auto* base = actor->GetActorBase();
  return base && base->GetFormID() == expectedBase;
}

RemoteFaceObservation Inspect(RE::Actor* actor)
{
  RemoteFaceObservation result;
  auto* object = actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
  auto* geometry = object ? object->AsGeometry() : nullptr;
  if (!geometry)
    return result;
  auto* property = geometry->GetGeometryRuntimeData()
                     .properties[RE::BSGeometry::States::kEffect].get();
  auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(property);
  if (!lighting || !lighting->material)
    return result;
  const auto feature = lighting->material->GetFeature();
  result.shader = static_cast<std::int32_t>(feature);
  if (feature == RE::BSShaderMaterial::Feature::kFaceGen) {
    auto* material = static_cast<RE::BSLightingShaderMaterialFacegen*>(lighting->material);
    result.texture = !!material->tintTexture;
  }
  return result;
}

bool DiagnosticBudget(bool apply) noexcept
{
  // Reserve a budget for completed regeneration records as well: repeated
  // internal ApplyMasks calls must not consume every completion diagnostic.
  static std::atomic<unsigned> applyUsed{ 0 };
  static std::atomic<unsigned> regenUsed{ 0 };
  auto& used = apply ? applyUsed : regenUsed;
  auto count = used.load(std::memory_order_relaxed);
  while (count < 8) {
    if (used.compare_exchange_weak(count, count + 1, std::memory_order_relaxed))
      return true;
  }
  return false;
}
}

RemoteFaceRegenResult RegenerateRemoteFace(
  RE::Actor* actor, std::uint32_t expectedBase,
  const std::shared_ptr<const RemoteFaceTintSnapshot>& snapshot)
{
  RemoteFaceRegenResult result;
  // IDs and signatures were checked against SKSE 2.2.6's shipped 1170 DLL
  // and versionlib-1-6-1170-0.bin; other runtimes keep their original path.
  if (!REL::Module::IsAE() ||
      REL::Module::get().version() != REL::Version(1, 6, 1170, 0) ||
      !ExactActor(actor, expectedBase) || !snapshot || snapshot->masks.empty())
    return result;
  for (const auto* mask : snapshot->masks)
    if (!mask)
      return result;
  if (!actor->Is3DLoaded()) {
    result.result = RemoteFaceRegenQueue::Result::Retry;
    return result;
  }
  auto* npc = actor->GetActorBase();
  auto* node = actor->GetFaceNodeSkinned();
  auto* part = npc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFace);
  auto* manager = RE::BSFaceGenManager::GetSingleton();
  if (!node || !part || !manager) {
    result.result = RemoteFaceRegenQueue::Result::Retry;
    return result;
  }
  result.before = Inspect(actor);
  {
    RemoteFaceTintScope scope(actor->GetFormID(), expectedBase, snapshot,
                              result.before.shader, result.before.texture);
    // CommonLib's name for SKSE's FaceGen::RegenerateHead. AE ID26838 maps
    // to 1170 RVA42BD90. All texture ownership stays inside the engine.
    result.engineCalled = true;
    manager->PrepareHeadPartForShaders(node, part, npc);
    result.applyCount = scope.ApplyCount();
  }
  if (!ExactActor(actor, expectedBase))
    return result;
  result.after = Inspect(actor);
  // A completed engine call is final even if it skipped mask composition.
  // Retrying the same ready head cannot bypass an engine gate and wastes FPS.
  // A later appearance or NiNode change may request a fresh regeneration.
  result.result = RemoteFaceRegenQueue::Result::Done;
  return result;
}

void LogRemoteFaceApply(const RemoteFaceTintScope::State& state) noexcept
{
  if (!DiagnosticBudget(true))
    return;
  try {
    const auto line = fmt::format("[HDN][caras-dev-v3] stage=apply actor={:08x} base={:08x} maskCount={} applyCount={} shader={} texture={}",
                                   state.actorId, state.baseId, state.snapshot->masks.size(),
                                   state.applyCount, state.shader, state.texture);
    logger::info("{}", line);
    ConsoleApi::WriteNativeDiagnosticLine(line);
  } catch (...) {}
}

void LogRemoteFaceRegen(std::uint32_t actor, std::uint32_t base,
                        std::size_t masks,
                        const RemoteFaceRegenResult& result) noexcept
{
  if (!DiagnosticBudget(false))
    return;
  try {
    const auto line = fmt::format("[HDN][caras-dev-v3] stage=regen actor={:08x} base={:08x} maskCount={} engineCalled={} applyCount={} shaderBefore={} textureBefore={} shaderAfter={} textureAfter={} result={}",
                                   actor, base, masks, result.engineCalled, result.applyCount, result.before.shader,
                                   result.before.texture, result.after.shader, result.after.texture,
                                   static_cast<int>(result.result));
    logger::info("{}", line);
    ConsoleApi::WriteNativeDiagnosticLine(line);
  } catch (...) {}
}
}
