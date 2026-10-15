// Actual D3D11 software-GPU readback, independent of Skyrim/CEF installation.
#include "../skyrim-platform/src/tilted/ui/ScopedOverlayBackbuffer.h"
#include <d3dcompiler.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
static void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}
static void hr(HRESULT result, const char* message)
{
  require(SUCCEEDED(result), message);
}
static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device, UINT w, UINT h)
{
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = w;
  desc.Height = h;
  desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> result;
  hr(device->CreateTexture2D(&desc, nullptr, &result), "create texture");
  return result;
}
static std::array<unsigned char, 4> pixel(ID3D11Device* device,
                                         ID3D11DeviceContext* context,
                                         ID3D11Texture2D* target,
                                         UINT x, UINT y)
{
  D3D11_TEXTURE2D_DESC desc{};
  target->GetDesc(&desc);
  desc.BindFlags = 0;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  hr(device->CreateTexture2D(&desc, nullptr, &staging), "staging texture");
  context->CopyResource(staging.Get(), target);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  hr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "read pixel");
  std::array<unsigned char, 4> result{};
  std::memcpy(result.data(), static_cast<unsigned char*>(mapped.pData) +
                static_cast<size_t>(y) * mapped.RowPitch + x * 4,
              result.size());
  context->Unmap(staging.Get(), 0);
  return result;
}

int main()
{
  try {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    hr(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                         nullptr, 0, D3D11_SDK_VERSION, &device, nullptr,
                         &context), "create software GPU");
    const char* shader = R"(
      struct V { float4 pos : SV_Position; float2 uv : TEXCOORD; };
      V vs(uint id : SV_VertexID) {
        V v; v.uv = float2((id << 1) & 2, id & 2);
        v.pos = float4(v.uv * float2(2,-2) + float2(-1,1), 0, 1);
        return v;
      }
      float4 ps(V v) : SV_Target {
        if (v.uv.x > 0.3) discard;
        return float4(0,1,0,1);
      }
    )";
    ComPtr<ID3DBlob> vb, pb;
    hr(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
                   "vs", "vs_5_0", 0, 0, &vb, nullptr), "compile vertex shader");
    hr(D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr,
                   "ps", "ps_5_0", 0, 0, &pb, nullptr), "compile pixel shader");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    hr(device->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(),
                                  nullptr, &vs), "vertex shader");
    hr(device->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(),
                                 nullptr, &ps), "pixel shader");
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    hr(device->CreateRasterizerState(&rd, &raster), "raster state");
    context->RSSetState(raster.Get());
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const std::array<std::array<UINT, 2>, 5> sizes{
      {{1920,1080}, {1920,1089}, {1366,768}, {1280,720}, {800,600}}
    };
    for (auto size : sizes) {
      const UINT w = size[0], h = size[1];
      auto final = texture(device.Get(), w, h);
      auto offscreen = texture(device.Get(), 256, 128);
      ComPtr<ID3D11RenderTargetView> finalView, wrongView;
      hr(device->CreateRenderTargetView(final.Get(), nullptr, &finalView),
         "final target view");
      hr(device->CreateRenderTargetView(offscreen.Get(), nullptr, &wrongView),
         "offscreen target view");
      const float blue[]{0,0,1,1};
      const D3D11_VIEWPORT wrongViewport{31,17,128,64,0,1};
      auto* wrong = wrongView.Get();
      context->ClearRenderTargetView(finalView.Get(), blue);
      context->OMSetRenderTargets(1, &wrong, nullptr);
      context->RSSetViewports(1, &wrongViewport);
      // Previous renderer: draws to inherited target. UI absent in final.
      context->Draw(3, 0);
      require(pixel(device.Get(),context.Get(),final.Get(),w/8,h/2)[2] == 255,
              "legacy offscreen failure must reproduce");
      for (int frame = 0; frame < 100; ++frame) {
        CEFUtils::ScopedOverlayBackbuffer bind(context.Get(), final.Get());
        require(bool(bind) && bind.Width()==w && bind.Height()==h,
                "bind final size");
        context->Draw(3, 0);
      }
      const auto left = pixel(device.Get(),context.Get(),final.Get(),w/8,h/2);
      const auto middle = pixel(device.Get(),context.Get(),final.Get(),w/2,h/2);
      require(left[0]==0 && left[1]==255 && left[2]==0,
              "CEF panel must reach final screen");
      require(middle[0]==0 && middle[1]==0 && middle[2]==255,
              "portrait outside panel must survive without full clear");
      ComPtr<ID3D11RenderTargetView> restored;
      context->OMGetRenderTargets(1, &restored, nullptr);
      require(restored.Get()==wrongView.Get(), "restore inherited target");
      D3D11_VIEWPORT viewport{};
      UINT count=1;
      context->RSGetViewports(&count, &viewport);
      require(count==1 && std::memcmp(&viewport,&wrongViewport,sizeof(viewport))==0,
              "restore inherited viewport");
      // No bound target is also a valid inherited state to restore.
      context->OMSetRenderTargets(0,nullptr,nullptr);
      context->RSSetViewports(0,nullptr);
      { CEFUtils::ScopedOverlayBackbuffer bind(context.Get(), final.Get());
        require(bool(bind), "bind from empty state"); }
      restored.Reset();
      context->OMGetRenderTargets(1,&restored,nullptr);
      require(!restored, "restore empty target");
      count=0; context->RSGetViewports(&count,nullptr);
      require(count==0, "restore empty viewport");
      std::printf("PASS WARP %ux%u: legacy failure, UI pixels, portrait, "
                  "100 frames, restoration, empty state\n",w,h);
    }
    { CEFUtils::ScopedOverlayBackbuffer invalid(context.Get(),nullptr);
      require(!invalid, "null target must not mutate state"); }
    context->ClearState();
    std::puts("ALL CEF COMPOSITOR WARP CHECKS PASS");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr,"FAIL %s\n",e.what());
    return 1;
  }
}
