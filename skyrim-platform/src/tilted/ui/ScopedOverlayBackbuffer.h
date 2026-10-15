#pragma once

#include <array>
#include <d3d11.h>
#include <wrl/client.h>

namespace CEFUtils {
// A Present callback can inherit an off-screen target or a partial viewport
// from the game or another presenter. Browser pixels belong on the actual
// swap-chain surface. Hold that surface only for this draw, so ResizeBuffers
// never has to contend with a cached reference owned by the browser.
class ScopedOverlayBackbuffer
{
public:
  ScopedOverlayBackbuffer(ID3D11DeviceContext* context,
                          ID3D11Texture2D* surface) noexcept
    : context_(context)
  {
    if (!context || !surface)
      return;
    Microsoft::WRL::ComPtr<ID3D11Device> device, surfaceDevice;
    context->GetDevice(device.GetAddressOf());
    surface->GetDevice(surfaceDevice.GetAddressOf());
    if (!device || device.Get() != surfaceDevice.Get())
      return;
    D3D11_TEXTURE2D_DESC description{};
    surface->GetDesc(&description);
    if (!description.Width || !description.Height ||
        FAILED(device->CreateRenderTargetView(surface, nullptr,
                                              target_.GetAddressOf())))
      return;
    width_ = description.Width;
    height_ = description.Height;
    context->OMGetRenderTargets(static_cast<UINT>(previousTargets_.size()),
                                previousTargets_.data(), &previousDepth_);
    context->OMGetRenderTargetsAndUnorderedAccessViews(
      0, nullptr, nullptr, 0, static_cast<UINT>(previousUavs_.size()),
      previousUavs_.data());
    context->RSGetViewports(&previousViewportCount_,
                            previousViewports_.data());
    auto* target = target_.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    const D3D11_VIEWPORT viewport{ 0, 0, static_cast<float>(width_),
                                  static_cast<float>(height_), 0, 1 };
    context->RSSetViewports(1, &viewport);
    bound_ = true;
  }

  ~ScopedOverlayBackbuffer()
  {
    if (!bound_)
      return;
    UINT count = 0;
    for (UINT i = 0; i < previousTargets_.size(); ++i)
      if (previousTargets_[i])
        count = i + 1;
    std::array<UINT, D3D11_PS_CS_UAV_REGISTER_COUNT> preserveCounters{};
    preserveCounters.fill(static_cast<UINT>(-1));
    context_->OMSetRenderTargetsAndUnorderedAccessViews(
      count, previousTargets_.data(), previousDepth_, count,
      static_cast<UINT>(previousUavs_.size()) - count,
      previousUavs_.data() + count, preserveCounters.data() + count);
    context_->RSSetViewports(previousViewportCount_,
                             previousViewports_.data());
    for (auto* target : previousTargets_)
      if (target)
        target->Release();
    for (auto* uav : previousUavs_)
      if (uav)
        uav->Release();
    if (previousDepth_)
      previousDepth_->Release();
  }

  ScopedOverlayBackbuffer(const ScopedOverlayBackbuffer&) = delete;
  ScopedOverlayBackbuffer& operator=(const ScopedOverlayBackbuffer&) = delete;
  explicit operator bool() const noexcept { return bound_; }
  UINT Width() const noexcept { return width_; }
  UINT Height() const noexcept { return height_; }

private:
  ID3D11DeviceContext* context_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
  std::array<ID3D11RenderTargetView*,
             D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> previousTargets_{};
  ID3D11DepthStencilView* previousDepth_ = nullptr;
  std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT>
    previousUavs_{};
  std::array<D3D11_VIEWPORT,
             D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
    previousViewports_{};
  UINT previousViewportCount_ =
    static_cast<UINT>(previousViewports_.size());
  UINT width_ = 0, height_ = 0;
  bool bound_ = false;
};
}
