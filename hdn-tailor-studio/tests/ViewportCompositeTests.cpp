// Pixel readback on an actual D3D11 WARP device, using the production quad.
#include "ViewportComposite.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <stdexcept>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

void check(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
void hr(HRESULT result, const char* message)
{
  check(SUCCEEDED(result), message);
}
std::array<unsigned char, 4> pixel(ID3D11Device* device,
                                   ID3D11DeviceContext* context,
                                   ID3D11Texture2D* target, UINT x, UINT y)
{
  D3D11_TEXTURE2D_DESC desc{};
  target->GetDesc(&desc);
  desc.BindFlags = 0;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> copy;
  hr(device->CreateTexture2D(&desc, nullptr, &copy), "staging");
  context->CopyResource(copy.Get(), target);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &mapped), "readback");
  std::array<unsigned char, 4> out{};
  std::memcpy(out.data(),
              static_cast<unsigned char*>(mapped.pData) +
                static_cast<size_t>(y) * mapped.RowPitch + x * 4,
              out.size());
  context->Unmap(copy.Get(), 0);
  return out;
}
int main()
{
  try {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                         D3D11_SDK_VERSION, &device, nullptr, &context),
       "WARP");
    auto compile = [&](const char* source, const char* entry,
                       const char* profile) {
      ComPtr<ID3DBlob> blob;
      hr(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
                    entry, profile, 0, 0, &blob, nullptr),
         "compile shader");
      return blob;
    };
    auto v = compile(hdn::studio::viewportQuadShader, "vs", "vs_5_0");
    auto p = compile(hdn::studio::viewportQuadShader, "ps", "ps_5_0");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> portrait, ui;
    hr(device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(),
                                  nullptr, &vs),
       "VS");
    hr(device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(),
                                 nullptr, &portrait),
       "PS");
    const char* uiSource = R"(
      struct V {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
      float4 ps(V v):SV_TARGET {
        if(v.uv.x>1.0/3.0 && v.uv.x<2.0/3.0 && v.uv.y>0.125 && v.uv.y<0.875) discard;
        return float4(0,1,0,1);
      }
    )";
    auto u = compile(uiSource, "ps", "ps_5_0");
    hr(device->CreatePixelShader(u->GetBufferPointer(), u->GetBufferSize(),
                                 nullptr, &ui),
       "UI PS");
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    hr(device->CreateRasterizerState(&rd, &raster), "raster");
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    hr(device->CreateSamplerState(&sd, &sampler), "sampler");
    auto* sample = sampler.Get();
    context->PSSetSamplers(0, 1, &sample);
    context->RSSetState(raster.Get());
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 1;
    td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const unsigned char bluePixel[]{ 0, 0, 255, 255 };
    D3D11_SUBRESOURCE_DATA data{ bluePixel, 4, 4 };
    ComPtr<ID3D11Texture2D> image;
    hr(device->CreateTexture2D(&td, &data, &image), "image");
    ComPtr<ID3D11ShaderResourceView> imageView;
    hr(device->CreateShaderResourceView(image.Get(), nullptr, &imageView),
       "image view");
    const std::array<std::array<UINT, 2>, 5> sizes{ { { 1920, 1080 },
                                                      { 1920, 1089 },
                                                      { 1366, 768 },
                                                      { 1280, 720 },
                                                      { 800, 600 } } };
    const float green[]{ 0, 1, 0, 1 }, black[]{ 0, 0, 0, 1 },
      red[]{ 1, 0, 0, 1 };
    for (auto size : sizes) {
      td.Width = size[0];
      td.Height = size[1];
      td.BindFlags = D3D11_BIND_RENDER_TARGET;
      ComPtr<ID3D11Texture2D> target;
      ComPtr<ID3D11RenderTargetView> targetView;
      hr(device->CreateTexture2D(&td, nullptr, &target), "target");
      hr(device->CreateRenderTargetView(target.Get(), nullptr, &targetView),
         "target view");
      auto* rt = targetView.Get();
      context->OMSetRenderTargets(1, &rt, nullptr);
      const D3D11_VIEWPORT box{
        size[0] / 3.f, size[1] / 8.f, size[0] / 3.f, size[1] * .75f, 0, 1
      };
      const D3D11_VIEWPORT whole{ 0, 0, float(size[0]), float(size[1]), 0, 1 };
      auto expect = [&](bool loading) {
        const auto left = pixel(device.Get(), context.Get(), target.Get(),
                                size[0] / 8, size[1] / 2);
        const auto top = pixel(device.Get(), context.Get(), target.Get(),
                               size[0] / 2, size[1] / 16);
        const auto center = pixel(device.Get(), context.Get(), target.Get(),
                                  size[0] / 2, size[1] / 2);
        check(left[1] == 255 && top[1] == 255,
              "menu/navigation pixels must survive");
        check(center[0] == 0 && center[1] == 0 &&
                center[2] == (loading ? 0 : 255),
              "portrait/loading pixels");
      };
      context->PSSetShader(portrait.Get(), nullptr, 0);
      // Reproduce the reported failure: a full clear erases prepainted UI.
      context->ClearRenderTargetView(rt, green);
      context->ClearRenderTargetView(rt, black);
      check(
        hdn::studio::compositeViewport(context.Get(), box, imageView.Get()),
        "legacy quad");
      check(pixel(device.Get(), context.Get(), target.Get(), size[0] / 8,
                  size[1] / 2)[1] == 0,
            "legacy erase reproduces");
      // UI first, portrait second. Outside the reserved box stays untouched.
      context->ClearRenderTargetView(rt, green);
      for (int frame = 0; frame < 100; ++frame)
        check(
          hdn::studio::compositeViewport(context.Get(), box, imageView.Get()),
          "quad");
      expect(false);
      // Portrait first, UI second, with transparent central DOM viewport.
      context->ClearRenderTargetView(rt, red);
      check(
        hdn::studio::compositeViewport(context.Get(), box, imageView.Get()),
        "portrait first");
      context->PSSetShader(ui.Get(), nullptr, 0);
      check(hdn::studio::compositeViewport(context.Get(), whole, nullptr),
            "UI second");
      expect(false);
      context->PSSetShader(portrait.Get(), nullptr, 0);
      context->ClearRenderTargetView(rt, green);
      check(hdn::studio::compositeViewport(context.Get(), box, nullptr),
            "loading quad");
      expect(true);
      auto invalid = box;
      invalid.Width = -1;
      check(!hdn::studio::compositeViewport(context.Get(), invalid,
                                            imageView.Get()),
            "reject invalid box");
      std::printf(
        "PASS viewport WARP %ux%u: reproduces legacy erase, preserves UI "
        "in both orders, portrait, loading, 100 frames\n",
        size[0], size[1]);
    }
    context->ClearState();
    std::puts("ALL VIEWPORT COMPOSITION PIXEL CHECKS PASS");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL %s\n", e.what());
    return 1;
  }
}
