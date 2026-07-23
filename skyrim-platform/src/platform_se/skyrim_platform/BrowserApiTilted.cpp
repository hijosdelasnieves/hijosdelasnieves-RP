#include <NirnLabUIPlatformAPI/API.h>

#include "BrowserApiTilted.h"
#include "NullPointerException.h"
#include "TPOverlayService.h"
#include <ui/DX11RenderHandler.h>

namespace {

thread_local bool g_cursorIsOpenByFocus = false;

inline CEFUtils::MyChromiumApp& GetApp()
{
  auto overlayService = OverlayService::GetInstance();
  if (!overlayService) {
    throw NullPointerException("overlayService");
  }

  auto app = overlayService->GetMyChromiumApp();
  if (!app) {
    throw NullPointerException("app");
  }

  return *app;
}

Napi::Object MakeHdnLoggerNativeBarResult(
  Napi::Env env,
  const CEFUtils::DX11RenderHandler::HdnLoggerNativeBarResult& state)
{
  auto result = Napi::Object::New(env);
  result.Set("supported", Napi::Boolean::New(env, state.supported));
  result.Set("active", Napi::Boolean::New(env, state.active));
  result.Set("running", Napi::Boolean::New(env, state.running));
  result.Set("accepted", Napi::Boolean::New(env, state.accepted));
  result.Set("attempt", Napi::Number::New(env, state.attempt));
  result.Set("value", Napi::Number::New(env, state.value));
  result.Set("elapsedMs", Napi::Number::New(env, state.elapsedMs));
  return result;
}
}

Napi::Value BrowserApiTilted::SetVisible(const Napi::CallbackInfo& info)
{
  bool& v = CEFUtils::DX11RenderHandler::Visible();
  v = NapiHelper::ExtractBoolean(info[0], "visible");
  return info.Env().Undefined();
}

Napi::Value BrowserApiTilted::IsVisibleJS(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), IsVisible());
}

bool BrowserApiTilted::IsVisible()
{
  return CEFUtils::DX11RenderHandler::Visible();
}

Napi::Value BrowserApiTilted::SetFocused(const Napi::CallbackInfo& info)
{
  bool& v = CEFUtils::DInputHook::ChromeFocus();
  bool newFocus = NapiHelper::ExtractBoolean(info[0], "focused");
  if (v != newFocus) {
    v = newFocus;

    auto ui = RE::UI::GetSingleton();
    auto msgQ = RE::UIMessageQueue::GetSingleton();

    if (!ui || !msgQ) {
      return info.Env().Undefined();
    }

    const bool alreadyOpen = ui->IsMenuOpen(RE::CursorMenu::MENU_NAME);

    if (newFocus) {
      if (!alreadyOpen) {
        msgQ->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow,
                         NULL);
        g_cursorIsOpenByFocus = true;
      }
    } else {
      if (g_cursorIsOpenByFocus) {
        msgQ->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide,
                         NULL);
        g_cursorIsOpenByFocus = false;
      }
    }
  }
  return info.Env().Undefined();
}

Napi::Value BrowserApiTilted::IsFocused(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), CEFUtils::DInputHook::ChromeFocus());
}

Napi::Value BrowserApiTilted::LoadUrl(const Napi::CallbackInfo& info)
{
  auto str = NapiHelper::ExtractString(info[0], "url");
  return Napi::Boolean::New(info.Env(), GetApp().LoadUrl(str.data()));
}

Napi::Value BrowserApiTilted::ExecuteJavaScript(const Napi::CallbackInfo& info)
{
  auto str = NapiHelper::ExtractString(info[0], "src");
  GetApp().ExecuteJavaScript(str);
  return info.Env().Undefined();
}

Napi::Value BrowserApiTilted::IsHdnLoggerNativeBarSupported(
  const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(
    info.Env(), CEFUtils::DX11RenderHandler::IsHdnLoggerNativeBarSupported());
}

Napi::Value BrowserApiTilted::StartHdnLoggerNativeBar(
  const Napi::CallbackInfo& info)
{
  const auto object = NapiHelper::ExtractObject(info[0], "hdnLoggerNativeBar");
  CEFUtils::DX11RenderHandler::HdnLoggerNativeBarConfig config;
  config.viewportWidth =
    NapiHelper::ExtractDouble(object.Get("viewportWidth"), "viewportWidth");
  config.viewportHeight =
    NapiHelper::ExtractDouble(object.Get("viewportHeight"), "viewportHeight");
  config.trackLeft =
    NapiHelper::ExtractDouble(object.Get("trackLeft"), "trackLeft");
  config.trackTop =
    NapiHelper::ExtractDouble(object.Get("trackTop"), "trackTop");
  config.trackWidth =
    NapiHelper::ExtractDouble(object.Get("trackWidth"), "trackWidth");
  config.trackHeight =
    NapiHelper::ExtractDouble(object.Get("trackHeight"), "trackHeight");
  config.barWidth =
    NapiHelper::ExtractDouble(object.Get("barWidth"), "barWidth");
  config.travelMs =
    NapiHelper::ExtractDouble(object.Get("travelMs"), "travelMs");
  config.startPhase =
    NapiHelper::ExtractDouble(object.Get("startPhase"), "startPhase");
  config.attempt = NapiHelper::ExtractUInt32(object.Get("attempt"), "attempt");
  return MakeHdnLoggerNativeBarResult(
    info.Env(), CEFUtils::DX11RenderHandler::StartHdnLoggerNativeBar(config));
}

Napi::Value BrowserApiTilted::StopHdnLoggerNativeBar(
  const Napi::CallbackInfo& info)
{
  const double minimumElapsedMs = info.Length() > 0 && !info[0].IsUndefined()
    ? NapiHelper::ExtractDouble(info[0], "minimumElapsedMs")
    : 0.0;
  return MakeHdnLoggerNativeBarResult(
    info.Env(),
    CEFUtils::DX11RenderHandler::StopHdnLoggerNativeBar(minimumElapsedMs));
}

Napi::Value BrowserApiTilted::ClearHdnLoggerNativeBar(
  const Napi::CallbackInfo& info)
{
  CEFUtils::DX11RenderHandler::ClearHdnLoggerNativeBar();
  return info.Env().Undefined();
}
