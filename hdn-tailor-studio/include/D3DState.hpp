#pragma once
#include <array>

namespace hdn::studio {
// Inventory's renderer is engine code. Preserve all graphics-pipeline state
// it may touch before handing the frame to SkyrimPlatform's CEF presenter.
// COM refs returned by *Get calls must be released, including null slots.
class D3DState {
  ID3D11DeviceContext* context_;
  template<class T, size_t N> static void release(std::array<T*, N>& array) {
    for (auto* ptr : array) if (ptr) ptr->Release();
  }
  template<class Shader> struct Stage {
    Shader* shader = nullptr;
    std::array<ID3D11ClassInstance*, 256> classes{};
    UINT count = 256;
    std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers{};
    std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources{};
    std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};
    void clear() {
      if (shader) shader->Release();
      release(classes); release(buffers); release(resources); release(samplers);
    }
  };
  Stage<ID3D11VertexShader> vs_;
  Stage<ID3D11PixelShader> ps_;
  Stage<ID3D11GeometryShader> gs_;
  Stage<ID3D11HullShader> hs_;
  Stage<ID3D11DomainShader> ds_;
  std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets_{};
  ID3D11DepthStencilView* depth_ = nullptr;
  ID3D11DepthStencilState* depthState_ = nullptr;
  UINT stencilRef_ = 0, sampleMask_ = 0;
  ID3D11BlendState* blend_ = nullptr;
  FLOAT blendFactor_[4]{};
  ID3D11RasterizerState* raster_ = nullptr;
  std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports_{};
  std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors_{};
  UINT viewportCount_ = static_cast<UINT>(viewports_.size());
  UINT scissorCount_ = static_cast<UINT>(scissors_.size());
  ID3D11InputLayout* layout_ = nullptr;
  ID3D11Buffer* index_ = nullptr;
  DXGI_FORMAT indexFormat_{};
  UINT indexOffset_ = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology_{};
  std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertices_{};
  std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> strides_{}, offsets_{};
public:
  D3DState(const D3DState&) = delete;
  D3DState& operator=(const D3DState&) = delete;
  explicit D3DState(ID3D11DeviceContext* context) : context_(context) {
#define HDN_GET_STAGE(PREFIX, MEMBER) \
    context_->PREFIX##GetShader(&MEMBER.shader, MEMBER.classes.data(), &MEMBER.count); \
    context_->PREFIX##GetConstantBuffers(0, static_cast<UINT>(MEMBER.buffers.size()), MEMBER.buffers.data()); \
    context_->PREFIX##GetShaderResources(0, static_cast<UINT>(MEMBER.resources.size()), MEMBER.resources.data()); \
    context_->PREFIX##GetSamplers(0, static_cast<UINT>(MEMBER.samplers.size()), MEMBER.samplers.data())
    HDN_GET_STAGE(VS, vs_); HDN_GET_STAGE(PS, ps_); HDN_GET_STAGE(GS, gs_);
    HDN_GET_STAGE(HS, hs_); HDN_GET_STAGE(DS, ds_);
#undef HDN_GET_STAGE
    context_->OMGetRenderTargets(static_cast<UINT>(targets_.size()), targets_.data(), &depth_);
    context_->OMGetDepthStencilState(&depthState_, &stencilRef_);
    context_->OMGetBlendState(&blend_, blendFactor_, &sampleMask_);
    context_->RSGetState(&raster_);
    context_->RSGetViewports(&viewportCount_, viewports_.data());
    context_->RSGetScissorRects(&scissorCount_, scissors_.data());
    context_->IAGetInputLayout(&layout_);
    context_->IAGetIndexBuffer(&index_, &indexFormat_, &indexOffset_);
    context_->IAGetPrimitiveTopology(&topology_);
    context_->IAGetVertexBuffers(0, static_cast<UINT>(vertices_.size()), vertices_.data(), strides_.data(), offsets_.data());
  }
  ~D3DState() {
    // Unbind preview resources before re-binding old render targets (hazards).
    std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> empty{};
    context_->PSSetShaderResources(0, static_cast<UINT>(empty.size()), empty.data());
    context_->OMSetRenderTargets(static_cast<UINT>(targets_.size()), targets_.data(), depth_);
    context_->OMSetDepthStencilState(depthState_, stencilRef_);
    context_->OMSetBlendState(blend_, blendFactor_, sampleMask_);
    context_->RSSetState(raster_);
    context_->RSSetViewports(viewportCount_, viewports_.data());
    context_->RSSetScissorRects(scissorCount_, scissors_.data());
    context_->IASetInputLayout(layout_);
    context_->IASetIndexBuffer(index_, indexFormat_, indexOffset_);
    context_->IASetPrimitiveTopology(topology_);
    context_->IASetVertexBuffers(0, static_cast<UINT>(vertices_.size()), vertices_.data(), strides_.data(), offsets_.data());
#define HDN_SET_STAGE(PREFIX, MEMBER) \
    context_->PREFIX##SetShader(MEMBER.shader, MEMBER.classes.data(), MEMBER.count); \
    context_->PREFIX##SetConstantBuffers(0, static_cast<UINT>(MEMBER.buffers.size()), MEMBER.buffers.data()); \
    context_->PREFIX##SetShaderResources(0, static_cast<UINT>(MEMBER.resources.size()), MEMBER.resources.data()); \
    context_->PREFIX##SetSamplers(0, static_cast<UINT>(MEMBER.samplers.size()), MEMBER.samplers.data()); \
    MEMBER.clear()
    HDN_SET_STAGE(VS, vs_); HDN_SET_STAGE(PS, ps_); HDN_SET_STAGE(GS, gs_);
    HDN_SET_STAGE(HS, hs_); HDN_SET_STAGE(DS, ds_);
#undef HDN_SET_STAGE
    release(targets_); release(vertices_);
    if (depth_) depth_->Release();
    if (depthState_) depthState_->Release();
    if (blend_) blend_->Release();
    if (raster_) raster_->Release();
    if (layout_) layout_->Release();
    if (index_) index_->Release();
  }
};
}
