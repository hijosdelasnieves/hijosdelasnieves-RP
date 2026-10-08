#include "PCH.hpp"
#include "Studio.hpp"
#include "D3DState.hpp"
#include <array>
#include <cwctype>

namespace hdn::studio {
namespace {
using Microsoft::WRL::ComPtr;
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
std::mutex mutex;
Policy policy;
RE::NiPointer<RE::NiAVObject> model;
RE::NiPoint3 center{};
float radius = 0;
bool enabled = false, hooked = false;
Present nextPresent = nullptr;
// Per-instance vtable, not a global detour of every D3D11 swap chain.
std::array<void*, 18> swapVtable{};

std::uint64_t now() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
}
void reset() {
  policy.end(policy.token());
  model.reset();
  radius = 0;
}

// Do not steal an inventory, magic, crafting, loading, book or mod-owned scene.
// Neither Clear3D nor a fabricated LoadedInventoryModel is used anywhere.
bool sceneIdle(RE::UI3DSceneManager* scene, RE::Inventory3DManager* inventory) {
  const auto* ui = RE::UI::GetSingleton();
  if (!ui || ui->numPausesGame || ui->numItemMenus || ui->numCustomRendering || ui->closingAllMenus)
    return false;
  if (!scene || !scene->camera || !inventory) return false;
  const auto& data = inventory->GetRuntimeData();
  if (data.loadTask || !data.loadedModels.empty() || inventory->tempRef) return false;
  // A non-empty scheme stack is evidence of another owner, even without a menu.
  if (!scene->lightSchemes.empty()) return false;
  for (const auto& node : scene->menuObjects) {
    if (node) for (const auto& child : node->children) if (child) return false;
  }
  return true;
}

// A single-frame, balanced borrow of Skyrim's interface renderer. The actual
// avatar node belongs only to this plugin and is detached before next Present.
// No actor/reference is moved to a cell or reparented out of the world.
class SceneBorrow {
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
  SceneBorrow(RE::UI3DSceneManager* scene, RE::Inventory3DManager* inventory, RE::NiAVObject* node)
    : scene_(scene), inventory_(inventory), node_(node), cameraPos_(scene->cachedCameraPos),
      itemPos_(inventory->itemPos), itemPosCopy_(inventory->itemPosCopy), cameraRot_(scene->cachedCameraRot),
      frustum_(scene->viewFrustum), cameraData_(scene->camera->GetRuntimeData2()),
      lightScheme_(scene->currentlightScheme), scale_(inventory->itemScale),
      scaleCopy_(inventory->itemScaleCopy), zoom_(inventory->GetRuntimeData().zoomProgress) {}
  void attach() {
    using Begin = void(*)(RE::Inventory3DManager*, RE::INTERFACE_LIGHT_SCHEME);
    static REL::Relocation<Begin> begin{RELOCATION_ID(50881, 51754)};
    begin(inventory_, RE::INTERFACE_LIGHT_SCHEME::kInventory);
    begun_ = true;
    scene_->AttachChild(node_, RE::INTERFACE_LIGHT_SCHEME::kInventory);
    attached_ = true;
    scene_->SetCameraFOV(40);
    scene_->SetCameraPosition({0, 0, 0});
    // Begin3D configures the inventory camera basis, not the gameplay camera.
  }
  ~SceneBorrow() {
    if (attached_) scene_->DetachChild(node_);
    if (begun_) {
      using End = void(*)(RE::Inventory3DManager*);
      static REL::Relocation<End> end{RELOCATION_ID(50883, 51756)};
      end(inventory_);
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

void draw(IDXGISwapChain* swap) {
  std::scoped_lock lock(mutex);
  if (policy.expire(now())) { model.reset(); radius = 0; }
  if (!policy.token() || !model || radius <= 0) return;
  auto* scene = RE::UI3DSceneManager::GetSingleton();
  auto* inventory = RE::Inventory3DManager::GetSingleton();
  if (!sceneIdle(scene, inventory)) { policy.rendered(false); return; }
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<ID3D11Texture2D> surface;
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(swap->GetDevice(IID_PPV_ARGS(device.GetAddressOf()))) ||
      FAILED(swap->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf()))) ||
      FAILED(device->CreateRenderTargetView(surface.Get(), nullptr, target.GetAddressOf()))) {
    policy.rendered(false); return;
  }
  device->GetImmediateContext(context.GetAddressOf());
  D3D11_TEXTURE2D_DESC description{};
  surface->GetDesc(&description);
  if (!context || !description.Width || !description.Height || description.SampleDesc.Count != 1) {
    policy.rendered(false); return;
  }
  D3DState saved(context.Get());
  SceneBorrow borrow(scene, inventory, model.get());
  borrow.attach();
  auto rig = fit(radius, static_cast<float>(description.Width) / description.Height,
    0.27f, 0.50f, policy.zoom());
  RE::NiMatrix3 rotation;
  rotation.SetEulerAnglesXYZ(0, 0, policy.yaw() * 3.14159265358979323846f / 180.0f);
  model->local.rotate = rotation;
  model->local.translate = RE::NiPoint3{rig.x, rig.y, rig.z} - rotation * center;
  RE::NiUpdateData update{};
  update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
  model->Update(update);
  auto* rawTarget = target.Get();
  context->OMSetRenderTargets(1, &rawTarget, nullptr);
  const float black[4]{0, 0, 0, 1};
  context->ClearRenderTargetView(rawTarget, black);
  inventory->Render();
  // This is render submission, NOT proof of visible pixels. Visual acceptance
  // (skin, tint, outfit, clipping, ordering) is deliberately still required.
  policy.rendered(true);
}

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swap, UINT interval, UINT flags) {
  if (!(flags & DXGI_PRESENT_TEST)) {
    try { draw(swap); }
    catch (const std::exception& error) {
      SKSE::log::error("Render exception; studio closed: {}", error.what());
      std::scoped_lock lock(mutex); reset();
    }
  }
  // We draw first; the verified SkyrimPlatform presenter draws CEF afterwards.
  return nextPresent(swap, interval, flags);
}

bool installHook() {
  auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
  auto* swap = renderer ? renderer->data.renderWindows[0].swapChain : nullptr;
  if (!swap) return false;
  auto*** object = reinterpret_cast<void***>(swap);
  auto** table = *object;
  HMODULE owner = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(table[8]), &owner)) return false;
  wchar_t path[MAX_PATH]{};
  if (!GetModuleFileNameW(owner, path, MAX_PATH)) return false;
  auto filename = std::filesystem::path(path).filename().wstring();
  std::transform(filename.begin(), filename.end(), filename.begin(), [](wchar_t c) { return std::towlower(c); });
  // Unknown Present chains are refused: never accidentally paint over CEF.
  if (filename != L"skyrimplatformimpl.dll" && filename != L"skyrimplatform.dll") {
    SKSE::log::warn("Unsupported Present chain; studio stays unavailable");
    return false;
  }
  std::copy_n(table, swapVtable.size(), swapVtable.begin());
  nextPresent = reinterpret_cast<Present>(table[8]);
  swapVtable[8] = reinterpret_cast<void*>(&present);
  if (InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(object), swapVtable.data(), table) != table)
    return false;
  return true;
}

std::int32_t apiVersion(RE::StaticFunctionTag*) { return enabled && hooked ? 1 : 0; }
std::int32_t beginSession(RE::StaticFunctionTag*) {
  std::scoped_lock lock(mutex);
  if (!enabled || !hooked) return 0;
  reset();
  return policy.begin(now());
}
bool snapshot(RE::StaticFunctionTag*, std::int32_t token, std::int32_t revision, std::int32_t signedId) {
  std::scoped_lock lock(mutex);
  const auto timestamp = now();
  if (!policy.select(token, revision, timestamp)) return false;
  model.reset(); radius = 0;
  // Only a temporary LOCAL actor. Never mutate player 0x14, static NPCs,
  // appearance, equipment, position, alpha, AI or references on the server.
  const auto id = static_cast<RE::FormID>(signedId);
  auto* actor = (id >> 24) == 0xFF ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
  auto* source = actor && !actor->IsDeleted() ? actor->Get3D(false) : nullptr;
  if (!source || !std::isfinite(source->worldBound.radius) || source->worldBound.radius <= 0)
    return policy.commit(token, revision, timestamp, false), false;
  RE::NiPointer<RE::NiAVObject> copy(source->Clone());
  if (!copy || copy.get() == source || copy->parent) {
    policy.commit(token, revision, timestamp, false); return false;
  }
  // Clone only rendered geometry, never a world Actor. The whole skeleton,
  // attached armor and face are copied by the engine's cloning process.
  copy->local = RE::NiTransform();
  copy->SetAppCulled(false);
  copy->GetFlags().set(RE::NiAVObject::Flag::kIgnoreFade);
  if (auto* fade = copy->AsFadeNode()) fade->GetRuntimeData().currentFade = 1;
  RE::NiUpdateData update{};
  update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
  copy->Update(update);
  if (!std::isfinite(copy->worldBound.radius) || copy->worldBound.radius <= 0) {
    policy.commit(token, revision, timestamp, false); return false;
  }
  center = copy->worldBound.center;
  radius = copy->worldBound.radius;
  model = std::move(copy);
  return policy.commit(token, revision, now(), true);
}
bool frame(RE::StaticFunctionTag*, std::int32_t token, float yaw, float zoom) {
  std::scoped_lock lock(mutex);
  return policy.frame(token, now(), yaw, zoom);
}
std::int32_t getStatus(RE::StaticFunctionTag*, std::int32_t token) {
  std::scoped_lock lock(mutex);
  return static_cast<std::int32_t>(policy.status(token, now()));
}
bool endSession(RE::StaticFunctionTag*, std::int32_t token) {
  std::scoped_lock lock(mutex);
  if (!policy.end(token)) return false;
  model.reset(); radius = 0;
  return true;
}
}

bool registerPapyrus(RE::BSScript::IVirtualMachine* vm) {
  if (!vm) return false;
  vm->RegisterFunction("ApiVersion", "HdnTailorStudio", apiVersion);
  vm->RegisterFunction("BeginSession", "HdnTailorStudio", beginSession);
  vm->RegisterFunction("Snapshot", "HdnTailorStudio", snapshot);
  vm->RegisterFunction("Frame", "HdnTailorStudio", frame);
  vm->RegisterFunction("GetStatus", "HdnTailorStudio", getStatus);
  vm->RegisterFunction("EndSession", "HdnTailorStudio", endSession);
  return true;
}
void onMessage(SKSE::MessagingInterface::Message* message) {
  if (!message) return;
  if (message->type == SKSE::MessagingInterface::kDataLoaded) {
    const auto config = std::filesystem::absolute("Data/SKSE/Plugins/HdnTailorStudio.ini");
    enabled = GetPrivateProfileIntW(L"Studio", L"EnableExperimental", 0, config.c_str()) == 1;
    // First proof-of-concept is deliberately restricted to HDN's SE 1.5.97.
    enabled = enabled && REL::Module::get().version() == SKSE::RUNTIME_SSE_1_5_97;
    hooked = enabled && installHook();
    SKSE::log::info("Experimental enabled={} hook={} API={}", enabled, hooked, apiVersion(nullptr));
  } else if (message->type == SKSE::MessagingInterface::kPreLoadGame ||
             message->type == SKSE::MessagingInterface::kNewGame) {
    std::scoped_lock lock(mutex); reset();
  }
}
}
