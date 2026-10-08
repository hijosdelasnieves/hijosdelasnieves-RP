// Independent studio, GPL-3.0. Actor access is MAIN-THREAD, one-shot,
// read-only. Present owns only a mesh handle and COM resources, never engine
// geometry.
#include "D3DState.hpp"
#include "PCH.hpp"
#include "PixelProbe.hpp"
#include "Studio.hpp"
#include "SwapChainTable.hpp"
#include <array>
#include <cwctype>
#include <d3dcompiler.h>

namespace hdn::studio {
namespace {
using Microsoft::WRL::ComPtr;
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
struct Framework
{
  bool (*init)(ID3D11Device*) = nullptr;
  void* (*capture)(RE::Actor*) = nullptr;
  bool (*render)(void*, float, float, float) = nullptr;
  ID3D11ShaderResourceView* (*texture)(void*) = nullptr;
  void (*destroy)(void*) = nullptr;
  bool bind()
  {
    const auto module = GetModuleHandleW(L"MeshRenderingFramework.dll");
    if (!module)
      return false;
#define HDN_MRF_BIND(member, name)                                            \
  member = reinterpret_cast<decltype(member)>(GetProcAddress(module, name))
    HDN_MRF_BIND(init, "HdnMesh_Init");
    HDN_MRF_BIND(capture, "HdnMesh_CaptureActor");
    HDN_MRF_BIND(render, "HdnMesh_Render");
    HDN_MRF_BIND(texture, "HdnMesh_Texture");
    HDN_MRF_BIND(destroy, "HdnMesh_Delete");
#undef HDN_MRF_BIND
    return init && capture && render && texture && destroy;
  }
} framework;
std::mutex mutex;
Policy policy;
void* model = nullptr;
bool enabled = false, hooked = false, dirty = false, probed = false;
float renderedAspect = 0;
Present nextPresent = nullptr;
std::array<void*, 41> swapVtable{};
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11VertexShader> vertexShader;
ComPtr<ID3D11PixelShader> pixelShader;
ComPtr<ID3D11SamplerState> sampler;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11RasterizerState> raster;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11Texture2D> probeTexture;
ComPtr<ID3D11Query> probeQuery;
bool probePending = false;
std::uint64_t probeDeadline = 0;

std::uint64_t now()
{
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch())
      .count());
}
void deleteModel()
{
  if (model)
    framework.destroy(std::exchange(model, nullptr));
  dirty = probed = probePending = false;
  probeTexture.Reset();
  probeQuery.Reset();
}
void reset()
{
  policy.end(policy.token());
  deleteModel();
}
bool initializeQuad(IDXGISwapChain* swap)
{
  ComPtr<ID3D11Device> current;
  if (FAILED(swap->GetDevice(IID_PPV_ARGS(&current))))
    return false;
  if (device && current.Get() != device.Get()) {
    // Do not silently migrate a live mesh to another presentation device.
    reset();
    enabled = false;
    return false;
  }
  if (vertexShader && pixelShader && sampler && blend && raster && depth)
    return true;
  device = current;
  device->GetImmediateContext(&context);
  constexpr auto shader = R"(
Texture2D image : register(t0);
SamplerState imageSampler : register(s0);
struct V { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
V vs(uint id:SV_VertexID) {
  V o;
  o.uv=float2((id<<1)&2,id&2);
  o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);
  return o;
}
float4 ps(V input):SV_TARGET {
  float4 c=image.Sample(imageSampler,input.uv);
  return float4(c.rgb,1);
}
)";
  ComPtr<ID3DBlob> vs, ps, errors;
  if (FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
                        "vs", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs,
                        &errors)) ||
      FAILED(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
                        "ps", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps,
                        &errors)))
    return false;
  if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(),
                                        vs->GetBufferSize(), nullptr,
                                        &vertexShader)) ||
      FAILED(device->CreatePixelShader(
        ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader)))
    return false;
  D3D11_SAMPLER_DESC sampling{};
  sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampling.AddressU = sampling.AddressV = sampling.AddressW =
    D3D11_TEXTURE_ADDRESS_CLAMP;
  sampling.MaxLOD = D3D11_FLOAT32_MAX;
  D3D11_BLEND_DESC blending{};
  blending.RenderTarget[0].RenderTargetWriteMask =
    D3D11_COLOR_WRITE_ENABLE_ALL;
  D3D11_RASTERIZER_DESC rasterizing{};
  rasterizing.FillMode = D3D11_FILL_SOLID;
  rasterizing.CullMode = D3D11_CULL_NONE;
  rasterizing.DepthClipEnable = true;
  D3D11_DEPTH_STENCIL_DESC depthTesting{};
  return SUCCEEDED(device->CreateSamplerState(&sampling, &sampler)) &&
    SUCCEEDED(device->CreateBlendState(&blending, &blend)) &&
    SUCCEEDED(device->CreateRasterizerState(&rasterizing, &raster)) &&
    SUCCEEDED(device->CreateDepthStencilState(&depthTesting, &depth));
}
// Nonblocking one-shot liveness probe. Black pixels or alpha alone cannot
// qualify as an avatar; does NOT prove face, outfit, pose or visual quality.
bool probe(ID3D11ShaderResourceView* resource)
{
  if (probed)
    return true;
  if (!probePending) {
    ComPtr<ID3D11Resource> input;
    resource->GetResource(&input);
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(input.As(&texture)))
      return false;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        description.Width > 2048 || description.Height > 2048)
      return false;
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = description.MiscFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    D3D11_QUERY_DESC queryDescription{ D3D11_QUERY_EVENT, 0 };
    if (FAILED(
          device->CreateTexture2D(&description, nullptr, &probeTexture)) ||
        FAILED(device->CreateQuery(&queryDescription, &probeQuery)))
      return false;
    context->CopyResource(probeTexture.Get(), texture.Get());
    context->End(probeQuery.Get());
    context->Flush();
    probePending = true;
    probeDeadline = now() + 1000;
    return false;
  }
  const auto ready = context->GetData(probeQuery.Get(), nullptr, 0,
                                      D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if (ready == S_FALSE && now() < probeDeadline)
    return false;
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (ready != S_OK ||
      FAILED(context->Map(probeTexture.Get(), 0, D3D11_MAP_READ,
                          D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
    policy.captureFailed();
    logger().warn("Independent texture probe failed or timed out");
    return false;
  }
  D3D11_TEXTURE2D_DESC description{};
  probeTexture->GetDesc(&description);
  const bool visible =
    hasVisiblePixels(static_cast<const std::uint8_t*>(mapped.pData),
                     mapped.RowPitch, description.Width, description.Height);
  context->Unmap(probeTexture.Get(), 0);
  probeTexture.Reset();
  probeQuery.Reset();
  probePending = false;
  if (!visible) {
    policy.captureFailed();
    logger().warn("Independent mesh produced no geometry pixels");
    return false;
  }
  probed = true;
  logger().info("Independent geometry pixels verified token={} revision={}; "
                "visual Skyrim acceptance still required",
                policy.token(), policy.revision());
  return true;
}
void draw(IDXGISwapChain* swap)
{
  std::scoped_lock lock(mutex);
  if (policy.expire(now()))
    deleteModel();
  if (!enabled || !policy.token())
    return;
  if (!initializeQuad(swap)) {
    policy.captureFailed();
    return;
  }
  ComPtr<ID3D11Texture2D> surface;
  if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&surface))))
    return;
  D3D11_TEXTURE2D_DESC description{};
  surface->GetDesc(&description);
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(device->CreateRenderTargetView(surface.Get(), nullptr, &target)))
    return;
  D3DState restore(context.Get());
  auto* renderTarget = target.Get();
  context->OMSetRenderTargets(1, &renderTarget, nullptr);
  const float black[4]{ 0, 0, 0, 1 };
  // Entire world is covered, even while loading: no player/world duplicate.
  // SkyrimPlatform's CEF draws AFTER this, so controls remain visible.
  context->ClearRenderTargetView(target.Get(), black);
  const auto view = policy.viewport();
  const float width = view.width * description.Width;
  const float height = view.height * description.Height;
  const float aspect = width / height;
  const auto status = policy.status(policy.token(), now());
  if (!model || status == Status::invalidModel || status == Status::loading)
    return;
  if (dirty || aspect != renderedAspect) {
    if (!framework.render(model, policy.yaw(), policy.zoom(), aspect)) {
      policy.captureFailed();
      logger().warn("Independent draw failed token={} revision={}",
                    policy.token(), policy.revision());
      return;
    }
    dirty = false;
    renderedAspect = aspect;
  }
  auto* resource = framework.texture(model);
  if (!resource)
    return;
  const D3D11_VIEWPORT viewport{ (view.x - view.width / 2) * description.Width,
                                 (view.y - view.height / 2) *
                                   description.Height,
                                 width,
                                 height,
                                 0,
                                 1 };
  context->RSSetViewports(1, &viewport);
  context->RSSetState(raster.Get());
  context->OMSetBlendState(blend.Get(), nullptr, 0xffffffff);
  context->OMSetDepthStencilState(depth.Get(), 0);
  context->IASetInputLayout(nullptr);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context->VSSetShader(vertexShader.Get(), nullptr, 0);
  context->PSSetShader(pixelShader.Get(), nullptr, 0);
  context->GSSetShader(nullptr, nullptr, 0);
  context->HSSetShader(nullptr, nullptr, 0);
  context->DSSetShader(nullptr, nullptr, 0);
  context->PSSetShaderResources(0, 1, &resource);
  auto* sample = sampler.Get();
  context->PSSetSamplers(0, 1, &sample);
  context->Draw(3, 0);
  if (probe(resource))
    policy.rendered(true);
}
HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* swap, UINT interval,
                                  UINT flags)
{
  if (!(flags & DXGI_PRESENT_TEST)) {
    try {
      draw(swap);
    } catch (const std::exception& error) {
      logger().error("Independent render exception: {}", error.what());
      std::scoped_lock lock(mutex);
      reset();
    }
  }
  return nextPresent(swap, interval, flags);
}
bool installHook()
{
  auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
  auto* swap = renderer
    ? reinterpret_cast<IDXGISwapChain*>(
        renderer->GetRuntimeData().renderWindows[0].swapChain)
    : nullptr;
  if (!swap || !framework.bind() || !initializeQuad(swap) ||
      !framework.init(device.Get()))
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
  if (filename != L"skyrimplatformimpl.dll" &&
      filename != L"skyrimplatform.dll") {
    logger().warn("Unsupported Present chain; no studio installed");
    return false;
  }
  std::copy_n(table, swapChainTableEntries(swap), swapVtable.begin());
  nextPresent = reinterpret_cast<Present>(table[8]);
  swapVtable[8] = reinterpret_cast<void*>(&present);
  return InterlockedCompareExchangePointer(
           reinterpret_cast<void* volatile*>(object), swapVtable.data(),
           table) == table;
}
std::int32_t apiVersion(RE::StaticFunctionTag*)
{
  std::scoped_lock lock(mutex);
  return enabled && hooked ? 2 : 0;
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
  if (!enabled || !hooked || !policy.select(token, revision, now()))
    return false;
  deleteModel();
  policy.captureWindow(token, revision, now());
  const auto id = static_cast<RE::FormID>(signedId);
  if ((id >> 24) != 0xff || !SKSE::GetTaskInterface()) {
    policy.commit(token, revision, now(), false);
    return false;
  }
  SKSE::GetTaskInterface()->AddTask([token, revision, id] {
    std::scoped_lock taskLock(mutex);
    if (!policy.live(token, now()) || policy.revision() != revision)
      return;
    auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* ui = RE::UI::GetSingleton();
    const bool admitted = actor && player && actor != player &&
      !actor->IsDeleted() && !actor->IsDisabled() && !actor->IsDead() &&
      actor->GetParentCell() == player->GetParentCell() &&
      actor->Get3D(false) && ui && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
    void* captured = nullptr;
    try {
      captured = admitted ? framework.capture(actor) : nullptr;
    } catch (const std::exception& error) {
      logger().error("Independent resource capture failed: {}", error.what());
    }
    if (!policy.commit(token, revision, now(), captured != nullptr)) {
      if (captured)
        framework.destroy(captured);
      return;
    }
    model = captured;
    dirty = model != nullptr;
    logger().info("Independent snapshot token={} revision={} actor=0x{:08x} "
                  "valid={} (main-thread, no retained Actor/NiObjects)",
                  token, revision, id, model != nullptr);
  });
  return true;
}
bool frame(RE::StaticFunctionTag*, std::int32_t token, float yaw, float zoom)
{
  std::scoped_lock lock(mutex);
  const float oldYaw = policy.yaw(), oldZoom = policy.zoom();
  if (!policy.frame(token, now(), yaw, zoom))
    return false;
  dirty = dirty || oldYaw != policy.yaw() || oldZoom != policy.zoom();
  return true;
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
  deleteModel();
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
    std::scoped_lock lock(mutex);
    enabled = true;
    hooked = installHook();
    logger().info("Isolated mesh enabled={} hook={} API={}", enabled, hooked,
                  enabled && hooked ? 2 : 0);
  } else if (message->type == SKSE::MessagingInterface::kPreLoadGame ||
             message->type == SKSE::MessagingInterface::kNewGame) {
    std::scoped_lock lock(mutex);
    reset();
  }
}
}
