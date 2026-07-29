#include "DInputHook.hpp"
#include "HdnLoggerNativeBarModel.h"
#include "TextToDraw.h"
#include <DX11RenderHandler.h>
#include <DirectXColors.h>
#include <DirectXTK/DDSTextureLoader.h>
#include <DirectXTK/SimpleMath.h>
#include <DirectXTK/WICTextureLoader.h>
#include <OverlayClient.h>
#include <algorithm>
#include <cmath>
#include <cmrc/cmrc.hpp>
#include <codecvt>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <spdlog/spdlog.h>
#include <string>

CMRC_DECLARE(skyrim_plugin_resources);

namespace CEFUtils {
std::mutex DX11RenderHandler::s_hdnLoggerNativeBarLock;
DX11RenderHandler::HdnLoggerNativeBarState
  DX11RenderHandler::s_hdnLoggerNativeBar;
std::atomic_bool DX11RenderHandler::s_hdnLoggerNativeBarRendererReady{ false };

DX11RenderHandler::DX11RenderHandler(Renderer* apRenderer) noexcept
  : m_pRenderer(apRenderer)
{
  // So we need to lock this until we have the window dimension as a background
  // CEF thread will attempt to get it before we have it
  m_createLock.lock();
  isCreateLock = true;
}

DX11RenderHandler::~DX11RenderHandler()
{
  s_hdnLoggerNativeBarRendererReady.store(false);
  ClearHdnLoggerNativeBar();
}

void DX11RenderHandler::Render(
  const ObtainTextsToDrawFunction& obtainTextsToDraw)
{
  // We need contexts first
  if (!m_pImmediateContext || !m_pContext) {
    Create();

    if (!m_pImmediateContext || !m_pContext)
      return;
  }

  // First of all we flush our deferred context in case we have updated the
  // texture
  {
    std::unique_lock<std::mutex> _(m_textureLock);

    Microsoft::WRL::ComPtr<ID3D11CommandList> pCommandList;
    const auto result = m_pContext->FinishCommandList(FALSE, &pCommandList);

    if (result == S_OK && pCommandList) {
      m_pImmediateContext->ExecuteCommandList(pCommandList.Get(), TRUE);
    }
  }
  GetRenderTargetSize();

  m_pSpriteBatch->Begin(DirectX::SpriteSortMode_Deferred,
                        m_pStates->NonPremultiplied());

  if (Visible()) {
    std::unique_lock<std::mutex> _(m_textureLock);

    if (m_pTextureView) {
      m_pSpriteBatch->Draw(m_pTextureView.Get(),
                           DirectX::SimpleMath::Vector2(0.f, 0.f), nullptr,
                           DirectX::Colors::White, 0.f);
    }
  }

  DrawHdnLoggerNativeBar();

  // obtainTextsToDraw is expected to do nothing if IsVisible is set to false
  // NB: this code is active even if browser backend is nirnlab (for now)
  obtainTextsToDraw([&](const TextToDraw& textToDraw) {
    static_assert(
      std::is_same_v<std::decay_t<decltype(textToDraw.string.c_str()[0])>,
                     wchar_t>);

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;

    auto& font = m_pFonts[conv.to_bytes(textToDraw.fontName)];

    if (!font)
      return;

    auto origin = DirectX::SimpleMath::Vector2(
                    font->MeasureString(textToDraw.string.c_str())) /
      2;

    DirectX::XMVECTORF32 color = { static_cast<float>(textToDraw.color[0]),
                                   static_cast<float>(textToDraw.color[1]),
                                   static_cast<float>(textToDraw.color[2]),
                                   static_cast<float>(textToDraw.color[3]) };

    font->DrawString(m_pSpriteBatch.get(), textToDraw.string.c_str(),
                     DirectX::XMFLOAT2(textToDraw.x, textToDraw.y), color,
                     textToDraw.rotation, origin, textToDraw.size,
                     textToDraw.effects, textToDraw.layerDepth);
  });

  bool& focusFlag = CEFUtils::DInputHook::ChromeFocus();

  if (Visible() && focusFlag) {
    if (m_pCursorTexture && m_cursorX >= 0 && m_cursorY >= 0) {
      // MenuScreenData and CEF use render-target pixel coordinates. Keep the
      // cursor texture hotspot on that exact point at every resolution and
      // size ultrawide cursors from the limiting axis instead of width alone.
      const auto cursorScale =
        std::min(m_width / 1920.f, m_height / 1080.f);
      m_pSpriteBatch->Draw(
        m_pCursorTexture.Get(),
        DirectX::SimpleMath::Vector2(m_cursorX, m_cursorY), nullptr,
        DirectX::Colors::White, 0.f, DirectX::SimpleMath::Vector2(24.f, 25.f),
        cursorScale);
    }
  }

  m_pSpriteBatch->End();
}

void DX11RenderHandler::Reset()
{
  Create();
}

void DX11RenderHandler::Create()
{
  s_hdnLoggerNativeBarRendererReady.store(false);
  m_pHdnLoggerBarPixel.Reset();

  const auto hr = m_pRenderer->GetSwapChain()->GetDevice(
    IID_ID3D11Device,
    reinterpret_cast<void**>(m_pDevice.ReleaseAndGetAddressOf()));

  if (FAILED(hr))
    return;

  m_pDevice->GetImmediateContext(m_pImmediateContext.ReleaseAndGetAddressOf());

  if (!m_pImmediateContext)
    return;

  GetRenderTargetSize();

  if (FAILED(m_pDevice->CreateDeferredContext(
        0, m_pContext.ReleaseAndGetAddressOf())))
    return;

  m_pSpriteBatch =
    std::make_unique<DirectX::SpriteBatch>(m_pImmediateContext.Get());

  m_pStates = std::make_unique<DirectX::CommonStates>(m_pDevice.Get());
  CreateHdnLoggerBarPixel();

  if (FAILED(DirectX::CreateWICTextureFromFile(
        m_pDevice.Get(), m_pParent->GetCursorPathPNG().c_str(), nullptr,
        m_pCursorTexture.ReleaseAndGetAddressOf()))) {
    DirectX::CreateDDSTextureFromFile(
      m_pDevice.Get(), m_pParent->GetCursorPathDDS().c_str(), nullptr,
      m_pCursorTexture.ReleaseAndGetAddressOf());
  }

  cmrc::file file;
  try {
    file = cmrc::skyrim_plugin_resources::get_filesystem().open(
      "assets/cursor.png");
  } catch (std::exception& e) {
    auto dir =
      cmrc::skyrim_plugin_resources::get_filesystem().iterate_directory("");
    std::stringstream ss;
    ss << e.what() << std::endl << std::endl;
    ss << "Root directory contents is: " << std::endl;
    for (auto entry : dir)
      ss << entry.filename() << std::endl;
    throw std::runtime_error(ss.str());
  }

  DirectX::CreateWICTextureFromMemory(
    m_pDevice.Get(), reinterpret_cast<const uint8_t*>(file.begin()),
    file.size(), nullptr, m_pCursorTexture.ReleaseAndGetAddressOf());

  std::unique_lock<std::mutex> _(m_textureLock);

  if (!m_pTexture)
    CreateRenderTexture();

  for (const auto& entry :
       std::filesystem::directory_iterator("Data/Platform/Fonts/")) {
    std::filesystem::path path = entry.path();

    if (path.extension().string() != ".spritefont")
      continue;

    spdlog::info("Font has been added - " + entry.path().stem().string());

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    auto widestrFontPath =
      converter.from_bytes(static_cast<std::string>(path.string()));

    const wchar_t* fontPath = widestrFontPath.c_str();

    m_pFonts[entry.path().stem().string()] =
      std::make_unique<DirectX::SpriteFont>(m_pDevice.Get(), fontPath);
  }
}

void DX11RenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser,
                                    CefRect& rect)
{
  std::scoped_lock _(m_createLock);

  rect = CefRect(0, 0, m_width, m_height);
}

void DX11RenderHandler::OnPaint(CefRefPtr<CefBrowser> browser,
                                PaintElementType type,
                                const RectList& dirtyRects, const void* buffer,
                                int width, int height)
{
  if (type == PET_VIEW && m_width == width && m_height == height) {
    std::unique_lock<std::mutex> _(m_textureLock);

    if (!m_pTexture) {
      CreateRenderTexture();
    }

    // Under MO2 for some reason, OnPaint called before context initialization
    if (!m_pContext) {
      return;
    }

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    const auto result = m_pContext->Map(
      m_pTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

    if (SUCCEEDED(result)) {
      const auto pDest = static_cast<uint8_t*>(mappedResource.pData);
      std::memcpy(pDest, buffer, width * height * 4);
      m_pContext->Unmap(m_pTexture.Get(), 0);
    } else {
      // We got no mapping, let's drop the context and reset the texture so
      // that we attempt to create a new one during the next frame
      m_pContext.Reset();
      m_pTexture.Reset();
      m_width = m_height = 0;
    }
  }
}

void DX11RenderHandler::GetRenderTargetSize()
{
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pRenderTargetView;

  m_pImmediateContext->OMGetRenderTargets(
    1, pRenderTargetView.ReleaseAndGetAddressOf(), nullptr);
  if (pRenderTargetView) {
    Microsoft::WRL::ComPtr<ID3D11Resource> pSrcResource;
    pRenderTargetView->GetResource(pSrcResource.ReleaseAndGetAddressOf());

    if (pSrcResource) {
      Microsoft::WRL::ComPtr<ID3D11Texture2D> pSrcBuffer;
      pSrcResource.As(&pSrcBuffer);

      D3D11_TEXTURE2D_DESC desc;
      pSrcBuffer->GetDesc(&desc);

      if ((m_width != desc.Width || m_height != desc.Height) && m_pParent) {
        m_width = desc.Width;
        m_height = desc.Height;

        if (isCreateLock) {
          // We now know the size of the viewport, we can let CEF get it
          m_createLock.unlock();
          isCreateLock = false;
        }

        {
          std::unique_lock<std::mutex> _(m_textureLock);

          m_pTexture.Reset();
          m_pTextureView.Reset();
        }

        if (m_pParent->GetBrowser())
          m_pParent->GetBrowser()->GetHost()->WasResized();
      }
    }
  }
}

void DX11RenderHandler::CreateRenderTexture()
{
  D3D11_TEXTURE2D_DESC textDesc;
  textDesc.Width = m_width;
  textDesc.Height = m_height;
  textDesc.MipLevels = textDesc.ArraySize = 1;
  textDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  textDesc.SampleDesc.Count = 1;
  textDesc.SampleDesc.Quality = 0;
  textDesc.Usage = D3D11_USAGE_DYNAMIC;
  textDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  textDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  textDesc.MiscFlags = 0;

  if (FAILED(m_pDevice->CreateTexture2D(&textDesc, nullptr,
                                        m_pTexture.ReleaseAndGetAddressOf())))
    return;

  D3D11_SHADER_RESOURCE_VIEW_DESC sharedResourceViewDesc = {};
  sharedResourceViewDesc.Format = textDesc.Format;
  sharedResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sharedResourceViewDesc.Texture2D.MipLevels = 1;

  if (FAILED(m_pDevice->CreateShaderResourceView(
        m_pTexture.Get(), &sharedResourceViewDesc,
        m_pTextureView.ReleaseAndGetAddressOf())))
    return;
}

void DX11RenderHandler::CreateHdnLoggerBarPixel()
{
  if (!m_pDevice) {
    return;
  }

  constexpr uint32_t whitePixel = 0xffffffff;
  D3D11_SUBRESOURCE_DATA initialData{};
  initialData.pSysMem = &whitePixel;
  initialData.SysMemPitch = sizeof(whitePixel);

  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = 1;
  desc.Height = 1;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> pixelTexture;
  if (FAILED(m_pDevice->CreateTexture2D(
        &desc, &initialData, pixelTexture.ReleaseAndGetAddressOf()))) {
    return;
  }
  if (FAILED(m_pDevice->CreateShaderResourceView(
        pixelTexture.Get(), nullptr,
        m_pHdnLoggerBarPixel.ReleaseAndGetAddressOf()))) {
    return;
  }
  s_hdnLoggerNativeBarRendererReady.store(true);
}

bool DX11RenderHandler::IsHdnLoggerNativeBarSupported() noexcept
{
  return s_hdnLoggerNativeBarRendererReady.load();
}

DX11RenderHandler::HdnLoggerNativeBarResult
DX11RenderHandler::SnapshotHdnLoggerNativeBarLocked(
  std::chrono::steady_clock::time_point now, bool accepted) noexcept
{
  HdnLoggerNativeBarResult result;
  result.supported = IsHdnLoggerNativeBarSupported();
  result.active = s_hdnLoggerNativeBar.active;
  result.running = s_hdnLoggerNativeBar.running;
  result.accepted = accepted;
  result.attempt = s_hdnLoggerNativeBar.config.attempt;

  if (!s_hdnLoggerNativeBar.active) {
    return result;
  }

  result.elapsedMs = (std::max)(0.0,
                                std::chrono::duration<double, std::milli>(
                                  now - s_hdnLoggerNativeBar.startedAt)
                                  .count());
  result.value = s_hdnLoggerNativeBar.running
    ? HdnLoggerBarValue(s_hdnLoggerNativeBar.config.startPhase,
                        result.elapsedMs, s_hdnLoggerNativeBar.config.travelMs)
    : s_hdnLoggerNativeBar.frozenValue;
  return result;
}

DX11RenderHandler::HdnLoggerNativeBarResult
DX11RenderHandler::StartHdnLoggerNativeBar(
  const HdnLoggerNativeBarConfig& config) noexcept
{
  std::lock_guard<std::mutex> _(s_hdnLoggerNativeBarLock);
  const auto now = std::chrono::steady_clock::now();
  if (!IsHdnLoggerNativeBarSupported() ||
      !IsHdnLoggerBarGeometryValid(
        config.viewportWidth, config.viewportHeight, config.trackLeft,
        config.trackTop, config.trackWidth, config.trackHeight,
        config.barWidth, config.travelMs, config.startPhase) ||
      config.attempt > 64) {
    s_hdnLoggerNativeBar = {};
    return SnapshotHdnLoggerNativeBarLocked(now, false);
  }

  s_hdnLoggerNativeBar.active = true;
  s_hdnLoggerNativeBar.running = true;
  s_hdnLoggerNativeBar.config = config;
  s_hdnLoggerNativeBar.frozenValue = HdnLoggerPingPong(config.startPhase);
  s_hdnLoggerNativeBar.startedAt = now;
  return SnapshotHdnLoggerNativeBarLocked(now, true);
}

DX11RenderHandler::HdnLoggerNativeBarResult
DX11RenderHandler::StopHdnLoggerNativeBar(double minimumElapsedMs) noexcept
{
  std::lock_guard<std::mutex> _(s_hdnLoggerNativeBarLock);
  const auto now = std::chrono::steady_clock::now();
  auto snapshot = SnapshotHdnLoggerNativeBarLocked(now, false);
  if (!snapshot.supported || !snapshot.active || !snapshot.running ||
      !std::isfinite(minimumElapsedMs) || minimumElapsedMs < 0.0 ||
      snapshot.elapsedMs < minimumElapsedMs) {
    return snapshot;
  }

  s_hdnLoggerNativeBar.frozenValue = snapshot.value;
  s_hdnLoggerNativeBar.running = false;
  return SnapshotHdnLoggerNativeBarLocked(now, true);
}

void DX11RenderHandler::ClearHdnLoggerNativeBar() noexcept
{
  std::lock_guard<std::mutex> _(s_hdnLoggerNativeBarLock);
  s_hdnLoggerNativeBar = {};
}

void DX11RenderHandler::DrawHdnLoggerNativeBar()
{
  if (!Visible() || !m_pHdnLoggerBarPixel || !m_width || !m_height) {
    return;
  }

  HdnLoggerNativeBarConfig config;
  double value = 0.0;
  {
    std::lock_guard<std::mutex> _(s_hdnLoggerNativeBarLock);
    const auto snapshot = SnapshotHdnLoggerNativeBarLocked(
      std::chrono::steady_clock::now(), false);
    if (!snapshot.active) {
      return;
    }
    config = s_hdnLoggerNativeBar.config;
    value = snapshot.value;
  }

  const double scaleX = static_cast<double>(m_width) / config.viewportWidth;
  const double scaleY = static_cast<double>(m_height) / config.viewportHeight;
  const double centerX =
    (config.trackLeft + value * config.trackWidth) * scaleX;
  const double halfBarWidth = config.barWidth * scaleX * 0.5;

  RECT borderRect{ static_cast<LONG>(std::lround(centerX - halfBarWidth)),
                   static_cast<LONG>(std::lround(config.trackTop * scaleY)),
                   static_cast<LONG>(std::lround(centerX + halfBarWidth)),
                   static_cast<LONG>(std::lround(
                     (config.trackTop + config.trackHeight) * scaleY)) };
  borderRect.left =
    std::clamp<LONG>(borderRect.left, 0, static_cast<LONG>(m_width));
  borderRect.right =
    std::clamp<LONG>(borderRect.right, 0, static_cast<LONG>(m_width));
  borderRect.top =
    std::clamp<LONG>(borderRect.top, 0, static_cast<LONG>(m_height));
  borderRect.bottom =
    std::clamp<LONG>(borderRect.bottom, 0, static_cast<LONG>(m_height));
  if (borderRect.right <= borderRect.left ||
      borderRect.bottom <= borderRect.top) {
    return;
  }

  constexpr DirectX::XMVECTORF32 borderColor{ { 1.0f, 0.965f, 0.78f, 0.88f } };
  constexpr DirectX::XMVECTORF32 fillColor{ { 0.863f, 0.682f, 0.263f, 1.0f } };
  m_pSpriteBatch->Draw(m_pHdnLoggerBarPixel.Get(), borderRect, nullptr,
                       borderColor);

  RECT fillRect = borderRect;
  const LONG insetX =
    (std::max<LONG>)(1, static_cast<LONG>(std::lround(scaleX)));
  const LONG insetY =
    (std::max<LONG>)(1, static_cast<LONG>(std::lround(scaleY)));
  fillRect.left += insetX;
  fillRect.right -= insetX;
  fillRect.top += insetY;
  fillRect.bottom -= insetY;
  if (fillRect.right > fillRect.left && fillRect.bottom > fillRect.top) {
    m_pSpriteBatch->Draw(m_pHdnLoggerBarPixel.Get(), fillRect, nullptr,
                         fillColor);
  }
}
}
