#include "HdnVanillaMenuPolicy.h"

#include <atomic>
#include <chrono>
#include <mutex>

#include <mhook-lib/mhook.h>

namespace HdnVanillaMenuPolicy {
namespace {

using AddUiMessage_t = void (*)(RE::UIMessageQueue*, const RE::BSFixedString&,
                                RE::UI_MESSAGE_TYPE, RE::IUIMessageData*);

std::atomic_uint32_t g_mask{ kFailClosedMask };
std::atomic_bool g_queueHookInstalled{ false };
std::atomic_bool g_inputFenceInstalled{ false };
std::atomic_uint64_t g_blockedShowCount{ 0 };
std::atomic_uint64_t g_blockedInputCount{ 0 };
std::atomic_uint64_t g_tweenRecoveryCount{ 0 };
std::atomic_uint64_t g_lastTweenHideAtMs{ 0 };
std::mutex g_installMutex;
AddUiMessage_t g_originalAddUiMessage = nullptr;

std::string_view AsView(const RE::BSFixedString& value) noexcept
{
  const auto* text = value.c_str();
  return text ? std::string_view(text) : std::string_view();
}

bool IsBlocked(const std::uint32_t flag) noexcept
{
  return flag != 0 && (g_mask.load(std::memory_order_acquire) & flag) != 0;
}

std::uint64_t SteadyNowMs() noexcept
{
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch())
      .count());
}

void HdnAddUiMessage(RE::UIMessageQueue* queue,
                     const RE::BSFixedString& menuName,
                     const RE::UI_MESSAGE_TYPE messageType,
                     RE::IUIMessageData* data)
{
  constexpr std::string_view tweenMenuName = "TweenMenu";
  const auto menuNameView = AsView(menuName);
  if (messageType == RE::UI_MESSAGE_TYPE::kHide &&
      menuNameView == tweenMenuName) {
    g_lastTweenHideAtMs.store(SteadyNowMs(), std::memory_order_release);
  }

  // This is the zero-frame authority barrier. menuOpen is emitted only after
  // this queue has already created/started the Scaleform menu, which leaves a
  // P -> Enter race. Dropping kShow here means no MagicMenu instance, no
  // category focus and no Flash frame exist for a second input to capture.
  if (messageType == RE::UI_MESSAGE_TYPE::kShow &&
      IsBlocked(MenuFlagForName(menuNameView))) {
    g_blockedShowCount.fetch_add(1, std::memory_order_relaxed);

    // Selecting Magic or Skills from TweenMenu queues its close/fade before
    // the destination kShow. Blocking only the destination leaves MenuMode
    // active over a black frame. Requeue TweenMenu through the original
    // function after that kHide; a direct hotkey does not open TAB because it
    // has neither an open TweenMenu nor a recent TweenMenu hide.
    auto* ui = RE::UI::GetSingleton();
    const auto nowMs = SteadyNowMs();
    const auto lastTweenHideAtMs =
      g_lastTweenHideAtMs.load(std::memory_order_acquire);
    const bool tweenWasJustHidden = lastTweenHideAtMs != 0 &&
      nowMs >= lastTweenHideAtMs && nowMs - lastTweenHideAtMs <= 1000;
    const bool tweenStillOpen =
      ui && ui->IsMenuOpen(RE::BSFixedString(tweenMenuName.data()));
    if (g_originalAddUiMessage && (tweenStillOpen || tweenWasJustHidden)) {
      g_originalAddUiMessage(queue, RE::BSFixedString(tweenMenuName.data()),
                             RE::UI_MESSAGE_TYPE::kShow, nullptr);
      g_tweenRecoveryCount.fetch_add(1, std::memory_order_relaxed);
      g_lastTweenHideAtMs.store(0, std::memory_order_release);
    }
    return;
  }

  if (g_originalAddUiMessage) {
    g_originalAddUiMessage(queue, menuName, messageType, data);
  }
}

bool IsBlockedInput(RE::InputEvent* event) noexcept
{
  if (!event || event->eventType != RE::INPUT_EVENT_TYPE::kButton) {
    return false;
  }

  const auto* userEvent = event->QUserEvent().c_str();
  if (!userEvent) {
    return false;
  }

  return IsBlocked(InputFlagForName(userEvent));
}

class HdnMenuOpenEventHandler final : public RE::MenuEventHandler
{
public:
  explicit HdnMenuOpenEventHandler(RE::MenuEventHandler* originalHandler_)
    : originalHandler(originalHandler_)
  {
  }

  bool CanProcess(RE::InputEvent* event) override
  {
    if (IsBlockedInput(event)) {
      g_blockedInputCount.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return originalHandler && originalHandler->CanProcess(event);
  }

  bool ProcessKinect(RE::KinectEvent* event) override
  {
    return originalHandler && originalHandler->ProcessKinect(event);
  }

  bool ProcessThumbstick(RE::ThumbstickEvent* event) override
  {
    return originalHandler && originalHandler->ProcessThumbstick(event);
  }

  bool ProcessMouseMove(RE::MouseMoveEvent* event) override
  {
    return originalHandler && originalHandler->ProcessMouseMove(event);
  }

  bool ProcessButton(RE::ButtonEvent* event) override
  {
    if (IsBlockedInput(event)) {
      g_blockedInputCount.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return originalHandler && originalHandler->ProcessButton(event);
  }

private:
  RE::MenuEventHandler* originalHandler;
};

HdnMenuOpenEventHandler* g_inputWrapper = nullptr;

}

bool Initialize() noexcept
{
  if (g_queueHookInstalled.load(std::memory_order_acquire)) {
    return true;
  }

  std::lock_guard lock(g_installMutex);
  if (g_queueHookInstalled.load(std::memory_order_relaxed)) {
    return true;
  }

  try {
    REL::Relocation<std::uintptr_t> target{
      RE::Offset::UIMessageQueue::AddMessage
    };
    g_originalAddUiMessage =
      reinterpret_cast<AddUiMessage_t>(target.address());
    if (!g_originalAddUiMessage ||
        Mhook_SetHook(reinterpret_cast<PVOID*>(&g_originalAddUiMessage),
                      reinterpret_cast<PVOID>(&HdnAddUiMessage)) != TRUE) {
      g_originalAddUiMessage = nullptr;
      logger::critical(
        "HDN vanilla authority: UIMessageQueue pre-show hook failed");
      return false;
    }

    g_queueHookInstalled.store(true, std::memory_order_release);
    logger::info(
      "HDN vanilla authority: UIMessageQueue pre-show hook installed "
      "fail-closed mask={:#x}",
      g_mask.load(std::memory_order_relaxed));
    return true;
  } catch (const std::exception& error) {
    logger::critical(
      "HDN vanilla authority: UIMessageQueue hook exception: {}",
      error.what());
  } catch (...) {
    logger::critical(
      "HDN vanilla authority: UIMessageQueue hook unknown exception");
  }
  return false;
}

bool EnsureInputFence() noexcept
{
  if (g_inputFenceInstalled.load(std::memory_order_acquire)) {
    return true;
  }

  std::lock_guard lock(g_installMutex);
  if (g_inputFenceInstalled.load(std::memory_order_relaxed)) {
    return true;
  }

  try {
    auto* menuControls = RE::MenuControls::GetSingleton();
    if (!menuControls || !menuControls->menuOpenHandler) {
      logger::warn(
        "HDN vanilla authority: MenuControls input handler not ready");
      return false;
    }

    auto* originalHandler =
      static_cast<RE::MenuEventHandler*>(menuControls->menuOpenHandler.get());
    g_inputWrapper = new HdnMenuOpenEventHandler(originalHandler);
    menuControls->RemoveHandler(originalHandler);
    menuControls->AddHandler(g_inputWrapper);
    g_inputFenceInstalled.store(true, std::memory_order_release);
    logger::info("HDN vanilla authority: MenuControls input fence installed");
    return true;
  } catch (const std::exception& error) {
    logger::critical("HDN vanilla authority: input fence exception: {}",
                     error.what());
  } catch (...) {
    logger::critical("HDN vanilla authority: input fence unknown exception");
  }
  return false;
}

void SetMask(const std::uint32_t mask) noexcept
{
  g_mask.store(mask & kKnownMask, std::memory_order_release);
  Initialize();
  EnsureInputFence();
}

void SetMagicBlocked(const bool blocked) noexcept
{
  // This export predates the authoritative bitmask. Deployed clients call
  // false during disconnect cleanup, where falling back to vanilla is exactly
  // what fail-closed must prevent. Keep the legacy call monotonic; controlled
  // reuse is available through SetMask.
  g_mask.fetch_or(kMagic, std::memory_order_release);
  if (!blocked) {
    logger::warn(
      "HDN vanilla authority: ignored legacy request to unblock MagicMenu");
  }
  Initialize();
  EnsureInputFence();
}

std::uint32_t GetMask() noexcept
{
  return g_mask.load(std::memory_order_acquire);
}

State GetState() noexcept
{
  return State{
    kPolicyVersion,
    GetMask(),
    g_queueHookInstalled.load(std::memory_order_acquire),
    g_inputFenceInstalled.load(std::memory_order_acquire),
    g_blockedShowCount.load(std::memory_order_relaxed),
    g_blockedInputCount.load(std::memory_order_relaxed),
  };
}

}
