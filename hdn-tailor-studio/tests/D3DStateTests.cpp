// Actual Windows D3D11 WARP tests. Not a Skyrim rendering/character test.
#include "D3DState.hpp"
#include "SwapChainTable.hpp"
#include <cstdlib>
#include <d3d11.h>
#include <iostream>
#include <wrl/client.h>
using hdn::studio::D3DState;
using Microsoft::WRL::ComPtr;
int checks = 0;
void check(bool value)
{
  ++checks;
  if (!value) {
    std::cerr << "D3D state check failed " << checks << '\n';
    std::exit(1);
  }
}
int main()
{
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                    nullptr, 0, D3D11_SDK_VERSION, &device,
                                    nullptr, &context)));
  D3D11_TEXTURE2D_DESC description{};
  description.Width = description.Height = 16;
  description.MipLevels = description.ArraySize =
    description.SampleDesc.Count = 1;
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.BindFlags = D3D11_BIND_RENDER_TARGET |
    D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11UnorderedAccessView> uav;
  check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &texture)));
  check(SUCCEEDED(
    device->CreateRenderTargetView(texture.Get(), nullptr, &target)));
  ComPtr<ID3D11Texture2D> second;
  check(SUCCEEDED(device->CreateTexture2D(&description, nullptr, &second)));
  check(
    SUCCEEDED(device->CreateUnorderedAccessView(second.Get(), nullptr, &uav)));
  auto* savedTarget = target.Get();
  auto* savedUav = uav.Get();
  const UINT counter = static_cast<UINT>(-1);
  context->OMSetRenderTargetsAndUnorderedAccessViews(
    1, &savedTarget, nullptr, 1, 1, &savedUav, &counter);
  const D3D11_VIEWPORT viewport{ 1, 2, 10, 11, 0.25f, 0.75f };
  context->RSSetViewports(1, &viewport);
  context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  for (unsigned iteration = 0; iteration < 100; ++iteration) {
    {
      D3DState restore(context.Get());
      context->ClearState();
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    ComPtr<ID3D11RenderTargetView> restoredTarget;
    ComPtr<ID3D11UnorderedAccessView> restoredUav;
    context->OMGetRenderTargetsAndUnorderedAccessViews(
      1, &restoredTarget, nullptr, 1, 1, &restoredUav);
    check(restoredTarget.Get() == savedTarget);
    check(restoredUav.Get() == savedUav);
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    context->IAGetPrimitiveTopology(&topology);
    check(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_VIEWPORT restored{};
    UINT count = 1;
    context->RSGetViewports(&count, &restored);
    check(count == 1 && restored.TopLeftX == viewport.TopLeftX &&
          restored.TopLeftY == viewport.TopLeftY &&
          restored.Width == viewport.Width &&
          restored.Height == viewport.Height &&
          restored.MinDepth == viewport.MinDepth &&
          restored.MaxDepth == viewport.MaxDepth);
  }
  context->ClearState();
  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  check(SUCCEEDED(device.As(&dxgiDevice)));
  check(SUCCEEDED(dxgiDevice->GetAdapter(&adapter)));
  check(SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))));
  DXGI_SWAP_CHAIN_DESC1 swapDescription{};
  swapDescription.Width = swapDescription.Height = 16;
  swapDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapDescription.SampleDesc.Count = 1;
  swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDescription.BufferCount = 2;
  swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swapDescription.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  ComPtr<IDXGISwapChain1> swap;
  check(SUCCEEDED(factory->CreateSwapChainForComposition(
    device.Get(), &swapDescription, nullptr, &swap)));
  const size_t entries = hdn::studio::swapChainTableEntries(swap.Get());
  check(entries >= 29 && entries <= 41);
  auto*** object = reinterpret_cast<void***>(swap.Get());
  auto** original = *object;
  std::array<void*, 41> replacement{};
  std::copy_n(original, entries, replacement.begin());
  *object = replacement.data();
  DXGI_SWAP_CHAIN_DESC1 restoredDescription{};
  check(SUCCEEDED(swap->GetDesc1(&restoredDescription)));
  check(restoredDescription.Width == 16 &&
        restoredDescription.BufferCount == 2);
  ComPtr<IDXGISwapChain3> modern;
  if (SUCCEEDED(swap.As(&modern))) {
    if (static_cast<IDXGISwapChain*>(modern.Get()) == swap.Get())
      check(entries >= 40);
    check(modern->GetCurrentBackBufferIndex() < 2);
  }
  ComPtr<IDXGISwapChain4> newest;
  if (SUCCEEDED(swap.As(&newest))) {
    if (static_cast<IDXGISwapChain*>(newest.Get()) == swap.Get())
      check(entries == 41);
    // Exercise the final method; HDR support is not required on WARP.
    newest->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
  }
  *object = original;
  std::cout << checks
            << " real D3D11 WARP state checks; NOT Skyrim acceptance\n";
}
