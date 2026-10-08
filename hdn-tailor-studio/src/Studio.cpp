#include "Studio.hpp"
#include "D3DState.hpp"
#include "PCH.hpp"
#include "PixelProbe.hpp"
#include "Runtime.hpp"
#include <array>
#include <cwctype>

namespace hdn::studio {
namespace {
using Microsoft::WRL::ComPtr;
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
std::mutex mutex;
Policy policy;
RE::NiPointer<RE::NiAVObject> model;
// Owning NiPointer, not a retained Actor*. Only the dedicated preview root.
RE::NiPointer<RE::NiAVObject> hiddenSource;
bool sourceWasCulled = false;
RE::NiPoint3 center{};
float radius = 0;
bool needsPixelProbe = false;
bool enabled = false, hooked = false;
Present nextPresent = nullptr;
// Per-instance vtable, not a global detour of every D3D11 swap chain.
std::array<void*, 18> swapVtable{};

std::uint64_t now()
{
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch())
      .count());
}
void releaseSource()
{
  if (hiddenSource && hiddenSource->GetAppCulled())
    hiddenSource->SetAppCulled(sourceWasCulled);
  hiddenSource.reset();
}
void reset()
{
  policy.end(policy.token());
  model.reset();
  releaseSource();
  radius = 0;
}

// Do not steal an inventory, magic, crafting, loading, book or mod-owned
// scene. Neither Clear3D nor a fabricated LoadedInventoryModel is used
// anywhere.
bool sceneIdle(RE::UI3DSceneManager* scene, RE::Inventory3DManager* inventory)
{
  const auto* ui = RE::UI::GetSingleton();
  if (!ui || ui->numPausesGame || ui->numItemMenus || ui->numCustomRendering ||
      ui->closingAllMenus)
    return false;
  if (!scene || !scene->camera || !inventory)
    return false;
  const auto& data = inventory->GetRuntimeData();
  if (data.loadTask || !data.loadedModels.empty() || inventory->tempRef)
    return false;
  // A non-empty scheme stack is evidence of another owner, even without a
  // menu.
  if (!scene->lightSchemes.empty())
    return false;
  for (const auto& node : scene->menuObjects) {
    if (node)
      for (const auto& child : node->children)
        if (child)
          return false;
  }
  return true;
}

// A single-frame, balanced borrow of Skyrim's interface renderer. The actual
// avatar node belongs only to this plugin and is detached before next Present.
// No actor/reference is moved to a cell or reparented out of the world.
class SceneBorrow
{
  RE::UI3DSceneManager* scene_;
  RE::Inventory3DManager* inventory_;
  RE::NiAVObject* node_;
  RE::NiPoint3 cameraPos_, itemPos_, itemPosCopy_;
  RE::NiMatrix3 cameraRot_;
  RE::NiFrustum frustum_;
  RE::NiCamera::RUNTIME_DATA2 cameraData_;
  RE::INTERFACE_LIGHT_SCHEME lightScheme_;
  float scale_, scaleCopy_, zoom_;
  bool begun_ = false, attached_ = false;

public:
  SceneBorrow(RE::UI3DSceneManager* scene, RE::Inventory3DManager* inventory,
              RE::NiAVObject* node)
    : scene_(scene)
    , inventory_(inventory)
    , node_(node)
    , cameraPos_(scene->cachedCameraPos)
    , itemPos_(inventory->itemPos)
    , itemPosCopy_(inventory->itemPosCopy)
    , cameraRot_(scene->cachedCameraRot)
    , frustum_(scene->viewFrustum)
    , cameraData_(scene->camera->GetRuntimeData2())
    , lightScheme_(scene->currentlightScheme)
    , scale_(inventory->itemScale)
    , scaleCopy_(inventory->itemScaleCopy)
    , zoom_(inventory->GetRuntimeData().zoomProgress)
  {
  }
  void attach()
  {
    inventory_->Begin3D(RE::INTERFACE_LIGHT_SCHEME::kInventory);
    begun_ = true;
    scene_->AttachChild(node_, RE::INTERFACE_LIGHT_SCHEME::kInventory);
    attached_ = true;
    scene_->SetCameraFOV(40);
    scene_->SetCameraPosition({ 0, 0, 0 });
    // Begin3D configures the inventory camera basis, not the gameplay camera.
  }
  ~SceneBorrow()
  {
    if (attached_)
      scene_->DetachChild(node_);
    if (begun_) {
      inventory_->End3D();
    }
    scene_->SetCameraPosition(cameraPos_);
    scene_->SetCameraRotate(cameraRot_);
    scene_->viewFrustum = frustum_;
    scene_->camera->GetRuntimeData2() = cameraData_;
    scene_->currentlightScheme = lightScheme_;
    inventory_->itemPos = itemPos_;
    inventory_->itemPosCopy = itemPosCopy_;
    inventory_->itemScale = scale_;
    inventory_->itemScaleCopy = scaleCopy_;
    inventory_->GetRuntimeData().zoomProgress = zoom_;
  }
};

bool probe(ID3D11Device* device, ID3D11DeviceContext* context,
           ID3D11Texture2D* surface, const D3D11_TEXTURE2D_DESC& source)
{
  if (source.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
      source.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
      source.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
      source.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
    return false;
  const auto bounds = policy.viewport();
  const auto left =
    static_cast<UINT>((bounds.x - bounds.width / 2) * source.Width);
  const auto top =
    static_cast<UINT>((bounds.y - bounds.height / 2) * source.Height);
  const auto right =
    std::min(source.Width,
             static_cast<UINT>((bounds.x + bounds.width / 2) * source.Width));
  const auto bottom = std::min(
    source.Height,
    static_cast<UINT>((bounds.y + bounds.height / 2) * source.Height));
  if (right <= left || bottom <= top)
    return false;
  auto description = source;
  description.Width = right - left;
  description.Height = bottom - top;
  description.MipLevels = description.ArraySize = 1;
  description.Usage = D3D11_USAGE_STAGING;
  description.BindFlags = description.MiscFlags = 0;
  description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&description, nullptr,
                                     staging.GetAddressOf())))
    return false;
  const D3D11_BOX box{ left, top, 0, right, bottom, 1 };
  context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, surface, 0, &box);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    return false;
  const bool visible =
    hasVisiblePixels(static_cast<const std::uint8_t*>(mapped.pData),
                     mapped.RowPitch, description.Width, description.Height);
  context->Unmap(staging.Get(), 0);
  return visible;
}

void draw(IDXGISwapChain* swap)
{
  std::scoped_lock lock(mutex);
  if (policy.expire(now())) {
    model.reset();
    releaseSource();
    radius = 0;
  }
  if (!policy.token())
    return;
  auto* scene = RE::UI3DSceneManager::GetSingleton();
  auto* inventory = RE::Inventory3DManager::GetSingleton();
  if (!sceneIdle(scene, inventory)) {
    policy.rendered(false);
    return;
  }
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11Texture2D> surface;
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(swap->GetDevice(IID_PPV_ARGS(device.GetAddressOf()))) ||
      FAILED(swap->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()))) ||
      FAILED(device->CreateRenderTargetView(surface.Get(), nullptr,
                                            target.GetAddressOf()))) {
    policy.rendered(false);
    return;
  }
  device->GetImmediateContext(context.GetAddressOf());
  D3D11_TEXTURE2D_DESC description{};
  surface->GetDesc(&description);
  if (!context || !description.Width || !description.Height ||
      description.SampleDesc.Count != 1) {
    policy.rendered(false);
    return;
  }
  D3DState saved(context.Get());
  auto* rawTarget = target.Get();
  context->OMSetRenderTargets(1, &rawTarget, nullptr);
  const float black[4]{ 0, 0, 0, 1 };
  context->ClearRenderTargetView(rawTarget, black);
  if (!model || radius <= 0)
    return; // Black also during loading; CEF draws next.
  SceneBorrow borrow(scene, inventory, model.get());
  borrow.attach();
  const auto& frustum = scene->viewFrustum;
  const auto rig =
    fitFrustum(radius, std::abs(frustum.fRight - frustum.fLeft) / 2,
               std::abs(frustum.fTop - frustum.fBottom) / 2, policy.viewport(),
               policy.zoom());
  if (rig.radius <= 0) {
    policy.rendered(false);
    return;
  }
  RE::NiMatrix3 rotation;
  rotation.SetEulerAnglesXYZ(0, 0,
                             policy.yaw() * 3.14159265358979323846f / 180.0f);
  model->local.rotate = rotation;
  model->local.translate =
    RE::NiPoint3{ rig.x, rig.y, rig.z } - rotation * center;
  RE::NiUpdateData update{};
  update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
  model->Update(update);
  // Begin3D may have changed the target; explicitly re-bind before rendering.
  context->OMSetRenderTargets(1, &rawTarget, nullptr);
  inventory->Render();
  // Read back once per selection, before the CEF presenter runs. Native menu
  // renderer gates can otherwise make Render() silently submit NO pixels.
  if (needsPixelProbe) {
    needsPixelProbe = false;
    if (!probe(device.Get(), context.Get(), surface.Get(), description)) {
      logger().warn("No visible geometry after native render; refusing ready "
                    "status (menu render gate or unsupported surface)");
      policy.captureFailed();
      return;
    }
    logger().info("First-frame geometry pixels detected; Skyrim visual "
                  "acceptance still required");
  }
  // Pixels alone do not prove correct skin, tint, outfit, clipping or facing.
  policy.rendered(true);
}

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swap, UINT interval,
                                  UINT flags)
{
  if (!(flags & DXGI_PRESENT_TEST)) {
    try {
      draw(swap);
    } catch (const std::exception& error) {
      logger().error("Render exception; studio closed: {}", error.what());
      std::scoped_lock lock(mutex);
      reset();
    }
  }
  // We draw first; the verified SkyrimPlatform presenter draws CEF afterwards.
  return nextPresent(swap, interval, flags);
}

bool installHook()
{
  auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
  auto* swap = renderer
    ? reinterpret_cast<IDXGISwapChain*>(
        renderer->GetRuntimeData().renderWindows[0].swapChain)
    : nullptr;
  if (!swap)
    return false;
  auto*** object = reinterpret_cast<void***>(swap);
  auto** table = *object;
  HMODULE owner = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(table[8]), &owner))
    return false;
  wchar_t path[MAX_PATH]{};
  if (!GetModuleFileNameW(owner, path, MAX_PATH))
    return false;
  auto filename = std::filesystem::path(path).filename().wstring();
  std::transform(filename.begin(), filename.end(), filename.begin(),
                 [](wchar_t c) { return std::towlower(c); });
  // Unknown Present chains are refused: never accidentally paint over CEF.
  if (filename != L"skyrimplatformimpl.dll" &&
      filename != L"skyrimplatform.dll") {
    logger().warn("Unsupported Present chain; studio stays unavailable");
    return false;
  }
  std::copy_n(table, swapVtable.size(), swapVtable.begin());
  nextPresent = reinterpret_cast<Present>(table[8]);
  swapVtable[8] = reinterpret_cast<void*>(&present);
  if (InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(object), swapVtable.data(), table) !=
      table)
    return false;
  return true;
}

std::int32_t apiVersion(RE::StaticFunctionTag*)
{
  return enabled && hooked ? 1 : 0;
}
std::int32_t beginSession(RE::StaticFunctionTag*)
{
  std::scoped_lock lock(mutex);
  if (!enabled || !hooked)
    return 0;
  reset();
  return policy.begin(now());
}
bool snapshot(RE::StaticFunctionTag*, std::int32_t token,
              std::int32_t revision, std::int32_t signedId)
{
  std::scoped_lock lock(mutex);
  const auto timestamp = now();
  if (!policy.select(token, revision, timestamp))
    return false;
  model.reset();
  radius = 0;
  releaseSource();
  // Only a temporary LOCAL actor. Never mutate player 0x14, static NPCs,
  // appearance, equipment, position, alpha, AI or references on the server.
  // Hide only this local clone's root after copying, to remove the world
  // double.
  const auto id = static_cast<RE::FormID>(signedId);
  auto* actor =
    (id >> 24) == 0xFF ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
  auto* source = actor && !actor->IsDeleted() ? actor->Get3D(false) : nullptr;
  if (!source || !std::isfinite(source->worldBound.radius) ||
      source->worldBound.radius <= 0)
    return policy.commit(token, revision, timestamp, false), false;
  // The updated SDK exposes Clone on NiObject. Retain ownership even if
  // the engine refuses to return a scene object; never cast blindly.
  RE::NiPointer<RE::NiObject> cloned(source->Clone());
  RE::NiPointer<RE::NiAVObject> copy(
    cloned ? netimmerse_cast<RE::NiAVObject*>(cloned.get()) : nullptr);
  if (!copy || copy.get() == source || copy->parent) {
    policy.commit(token, revision, timestamp, false);
    return false;
  }
  // Clone only rendered geometry, never a world Actor. The whole skeleton,
  // attached armor and face are copied by the engine's cloning process.
  copy->local = RE::NiTransform();
  copy->SetAppCulled(false);
  copy->GetFlags().set(RE::NiAVObject::Flag::kIgnoreFade);
  if (auto* fade = copy->AsFadeNode())
    fade->GetRuntimeData().currentFade = 1;
  RE::NiUpdateData update{};
  update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
  copy->Update(update);
  if (!std::isfinite(copy->worldBound.radius) ||
      copy->worldBound.radius <= 0) {
    policy.commit(token, revision, timestamp, false);
    return false;
  }
  center = copy->worldBound.center;
  radius = copy->worldBound.radius;
  model = std::move(copy);
  needsPixelProbe = true;
  hiddenSource.reset(source);
  sourceWasCulled = source->GetAppCulled();
  source->SetAppCulled(true);
  if (!policy.commit(token, revision, now(), true)) {
    model.reset();
    releaseSource();
    radius = 0;
    return false;
  }
  return true;
}
bool frame(RE::StaticFunctionTag*, std::int32_t token, float yaw, float zoom)
{
  std::scoped_lock lock(mutex);
  return policy.frame(token, now(), yaw, zoom);
}
bool viewport(RE::StaticFunctionTag*, std::int32_t token, float x, float y,
              float width, float height)
{
  std::scoped_lock lock(mutex);
  return policy.viewport(token, now(), { x, y, width, height });
}
std::int32_t getStatus(RE::StaticFunctionTag*, std::int32_t token)
{
  std::scoped_lock lock(mutex);
  return static_cast<std::int32_t>(policy.status(token, now()));
}
bool endSession(RE::StaticFunctionTag*, std::int32_t token)
{
  std::scoped_lock lock(mutex);
  if (!policy.end(token))
    return false;
  model.reset();
  radius = 0;
  releaseSource();
  return true;
}
}

bool registerPapyrus(RE::BSScript::IVirtualMachine* vm)
{
  if (!vm)
    return false;
  vm->RegisterFunction("ApiVersion", "HdnTailorStudio", apiVersion);
  vm->RegisterFunction("BeginSession", "HdnTailorStudio", beginSession);
  vm->RegisterFunction("Snapshot", "HdnTailorStudio", snapshot);
  vm->RegisterFunction("Frame", "HdnTailorStudio", frame);
  vm->RegisterFunction("Viewport", "HdnTailorStudio", viewport);
  vm->RegisterFunction("GetStatus", "HdnTailorStudio", getStatus);
  vm->RegisterFunction("EndSession", "HdnTailorStudio", endSession);
  return true;
}
void onMessage(SKSE::MessagingInterface::Message* message)
{
  if (!message)
    return;
  if (message->type == SKSE::MessagingInterface::kDataLoaded) {
    const auto config =
      std::filesystem::absolute("Data/SKSE/Plugins/HdnTailorStudio.ini");
    enabled = GetPrivateProfileIntW(L"Studio", L"EnableExperimental", 0,
                                    config.c_str()) == 1;
    const auto runtime = REL::Module::get().version();
    enabled = enabled &&
      supportedRuntime({ runtime[0], runtime[1], runtime[2], runtime[3] });
    hooked = enabled && installHook();
    logger().info("Experimental enabled={} hook={} API={}", enabled, hooked,
                  apiVersion(nullptr));
  } else if (message->type == SKSE::MessagingInterface::kPreLoadGame ||
             message->type == SKSE::MessagingInterface::kNewGame) {
    std::scoped_lock lock(mutex);
    reset();
  }
}
}
