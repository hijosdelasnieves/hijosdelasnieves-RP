#include "RemoteFaceTintRenderer.h"

#include <RE/A/Actor.h>
#include <RE/B/BGSHeadPart.h>
#include <RE/B/BSGeometry.h>
#include <RE/B/BSLightingShaderMaterialFacegen.h>
#include <RE/B/BSLightingShaderProperty.h>
#include <RE/N/NiRTTI.h>
#include <RE/N/NiSourceTexture.h>
#include <RE/R/Renderer.h>
#include <REL/Relocation.h>

#include <cstddef>
#include <atomic>

namespace Hdn {
namespace {
// The AE renderer ABI is independently documented by Skyrim Together's
// FaceGenSystem (4917189f6ac382e874f9ee735c4fef6a350e449e). Its
// NiRenderedTexture::buffer and BSMaskedShaderMaterial::renderedTexture are
// the same slots named rendererTexture and tintTexture by CommonLibSSE.
static_assert(offsetof(RE::NiSourceTexture, rendererTexture) == 0x48);
static_assert(offsetof(RE::BSLightingShaderMaterialFacegen, tintTexture) == 0xA0);
static_assert(sizeof(RE::NiPointer<RE::NiSourceTexture>) == sizeof(void*));

using CreateRenderedTexture = RE::NiSourceTexture* (*)(RE::BSFixedString&);

bool IsExactDynamicAppearanceActor(RE::Actor* actor,
                                   std::uint32_t expectedBaseId)
{
  if (!actor || actor->GetFormID() < 0xFF000000 ||
      expectedBaseId < 0xFF000000 || actor->IsDeleted() ||
      actor->IsMarkedForDeletion()) {
    return false;
  }
  const auto base = actor->GetActorBase();
  return base && base->GetFormID() == expectedBaseId;
}
}

RemoteFaceTintResult RenderRemoteFaceTints(
  RE::Actor* actor, std::uint32_t expectedBaseId,
  const RE::BSTArray<RE::TintMask*>& masks)
{
  // The published HDN runtime is 1.6.1170 (AE). No SE/VR relocation IDs are
  // guessed: leave those runtimes on their existing path.
  if (!REL::Module::IsAE()) {
    return RemoteFaceTintResult::Drop;
  }
  if (!IsExactDynamicAppearanceActor(actor, expectedBaseId)) {
    return RemoteFaceTintResult::Drop;
  }
  if (masks.empty()) {
    return RemoteFaceTintResult::Drop;
  }
  for (const auto* mask : masks) {
    if (!mask) {
      return RemoteFaceTintResult::Drop;
    }
  }
  if (!actor->Is3DLoaded()) {
    return RemoteFaceTintResult::Retry;
  }

  auto* headObject = actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace);
  auto* headGeometry = headObject ? headObject->AsGeometry() : nullptr;
  if (!headGeometry) {
    return RemoteFaceTintResult::Retry;
  }
  RE::NiPointer<RE::BSGeometry> geometry(headGeometry);
  auto* property = geometry->GetGeometryRuntimeData()
                     .properties[RE::BSGeometry::States::kEffect].get();
  auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(property);
  if (!lighting || !lighting->material) {
    return RemoteFaceTintResult::Retry;
  }
  RE::NiPointer<RE::BSLightingShaderProperty> propertyLifetime(lighting);
  const auto feature = lighting->material->GetFeature();
  const auto featureNumber = static_cast<std::int32_t>(feature);
  if (feature != RE::BSShaderMaterial::Feature::kFaceGen) {
    // RGBTint (5) stores a NiColor at A0, not a NiPointer. Never write a
    // texture there or change shader features to make the cast appear valid.
    static std::atomic<std::uint32_t> unsupportedLogs{ 0 };
    if (unsupportedLogs.fetch_add(1, std::memory_order_relaxed) < 16) {
      logger::warn("[HDN][remote-face-tints] unsupported material feature={}",
                   featureNumber);
    }
    return RemoteFaceTintResult::Drop;
  }
  auto* material = static_cast<RE::BSLightingShaderMaterialFacegen*>(lighting->material);
  RE::BSTSmartPointer<RE::BSShaderMaterial> materialLifetime(material);

  auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!renderer) {
    return RemoteFaceTintResult::Retry;
  }
  static REL::Relocation<CreateRenderedTexture> createTexture{ REL::ID(70717) };
  RE::BSFixedString emptyName("");
  RE::NiPointer<RE::NiSourceTexture> texture(createTexture(emptyName));
  if (!texture) {
    return RemoteFaceTintResult::Retry;
  }
  // A newly created rendered texture has no resource. Avoid overwriting an
  // owned buffer if a future renderer ABI changes that contract.
  if (texture->rendererTexture) {
    return RemoteFaceTintResult::Retry;
  }
  auto* renderedResource = renderer->CreateRenderTexture(512, 512);
  if (!renderedResource) {
    return RemoteFaceTintResult::Retry;
  }
  texture->rendererTexture =
    reinterpret_cast<decltype(texture->rendererTexture)>(renderedResource);

  Frida::ApplyRemoteFaceTintMasks(actor->GetFormID(), masks, texture.get());

  // Keep the old material visible unless generation completes for the same
  // actor, geometry and material that were validated above.
  if (!IsExactDynamicAppearanceActor(actor, expectedBaseId) ||
      actor->GetHeadPartObject(RE::BGSHeadPart::HeadPartType::kFace) != headObject ||
      lighting->material != material) {
    return RemoteFaceTintResult::Retry;
  }
  // NiPointer::reset releases any previous GPU texture and retains this one.
  // The local smart pointer then releases its temporary reference.
  material->tintTexture.reset(texture.get());
  return RemoteFaceTintResult::Applied;
}
}
