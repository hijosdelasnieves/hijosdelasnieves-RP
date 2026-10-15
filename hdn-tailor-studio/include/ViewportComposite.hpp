#pragma once
#include <cmath>
#include <d3d11.h>

namespace hdn::studio {
inline constexpr auto viewportQuadShader = R"(
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

// The opaque portrait/background is confined to the DOM's reserved rectangle.
// In particular, never clear the full backbuffer: another UI backend may have
// already drawn the navigation and side panels before Present is called.
inline bool compositeViewport(ID3D11DeviceContext* context,
                              const D3D11_VIEWPORT& viewport,
                              ID3D11ShaderResourceView* image)
{
  if (!context || !std::isfinite(viewport.TopLeftX) ||
      !std::isfinite(viewport.TopLeftY) || !std::isfinite(viewport.Width) ||
      !std::isfinite(viewport.Height) || viewport.TopLeftX < 0 ||
      viewport.TopLeftY < 0 || viewport.Width <= 0 || viewport.Height <= 0)
    return false;
  context->RSSetViewports(1, &viewport);
  context->PSSetShaderResources(0, 1, &image);
  context->Draw(3, 0);
  return true;
}
}
