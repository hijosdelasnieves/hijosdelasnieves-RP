#pragma once

#include "MyRenderHandler.h"
#include "TextToDraw.h"
#include <DirectXTK/CommonStates.h>
#include <DirectXTK/SpriteBatch.h>
#include <DirectXTK/SpriteFont.h>
#include <Signal.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <wrl.h>

struct IDXGISwapChain;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11DeviceContext;
struct ID3D11Device;

namespace CEFUtils {
struct DX11RenderHandler : MyRenderHandler
{
  static bool& Visible()
  {
    static bool g_visible = false;
    return g_visible;
  }

  struct Renderer
  {
    Renderer() = default;
    virtual ~Renderer() = default;
    [[nodiscard]] virtual IDXGISwapChain* GetSwapChain() const noexcept = 0;

    TP_NOCOPYMOVE(Renderer);
  };

  explicit DX11RenderHandler(Renderer* apRenderer) noexcept;
  virtual ~DX11RenderHandler();

  TP_NOCOPYMOVE(DX11RenderHandler);

  void Create() override;
  void Render(const ObtainTextsToDrawFunction& obtainTextsToDraw) override;
  void Reset() override;

  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
               const RectList& dirtyRects, const void* buffer, int width,
               int height) override;

  struct HdnLoggerNativeBarConfig
  {
    double viewportWidth{ 0.0 };
    double viewportHeight{ 0.0 };
    double trackLeft{ 0.0 };
    double trackTop{ 0.0 };
    double trackWidth{ 0.0 };
    double trackHeight{ 0.0 };
    double barWidth{ 6.0 };
    double travelMs{ 1388.8889 };
    double startPhase{ 0.04 };
    uint32_t attempt{ 0 };
  };

  struct HdnLoggerNativeBarResult
  {
    bool supported{ false };
    bool active{ false };
    bool running{ false };
    bool accepted{ false };
    uint32_t attempt{ 0 };
    double value{ 0.0 };
    double elapsedMs{ 0.0 };
  };

  static bool IsHdnLoggerNativeBarSupported() noexcept;
  static HdnLoggerNativeBarResult StartHdnLoggerNativeBar(
    const HdnLoggerNativeBarConfig& config) noexcept;
  static HdnLoggerNativeBarResult StopHdnLoggerNativeBar(
    double minimumElapsedMs) noexcept;
  static void ClearHdnLoggerNativeBar() noexcept;

  IMPLEMENT_REFCOUNTING(DX11RenderHandler);

protected:
  void GetRenderTargetSize();
  void CreateRenderTexture();
  void CreateHdnLoggerBarPixel();
  void DrawHdnLoggerNativeBar();

private:
  struct HdnLoggerNativeBarState
  {
    bool active{ false };
    bool running{ false };
    HdnLoggerNativeBarConfig config;
    double frozenValue{ 0.0 };
    std::chrono::steady_clock::time_point startedAt;
  };

  static HdnLoggerNativeBarResult SnapshotHdnLoggerNativeBarLocked(
    std::chrono::steady_clock::time_point now, bool accepted) noexcept;

  uint32_t m_width{ 0 };
  uint32_t m_height{ 0 };

  Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pTexture;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pCursorTexture;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pTextureView;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_pHdnLoggerBarPixel;
  std::mutex m_textureLock;
  std::mutex m_createLock;
  bool isCreateLock = false;
  Renderer* m_pRenderer;

  Microsoft::WRL::ComPtr<ID3D11Device> m_pDevice;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pContext;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_pImmediateContext;

  std::unique_ptr<::DirectX::SpriteBatch> m_pSpriteBatch;

  std::unique_ptr<::DirectX::CommonStates> m_pStates;

  std::map<std::string, std::unique_ptr<::DirectX::SpriteFont>> m_pFonts;

  static std::mutex s_hdnLoggerNativeBarLock;
  static HdnLoggerNativeBarState s_hdnLoggerNativeBar;
  static std::atomic_bool s_hdnLoggerNativeBarRendererReady;
};
}
