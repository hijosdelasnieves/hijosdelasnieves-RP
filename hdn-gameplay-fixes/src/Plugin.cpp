// GPL-3.0-or-later. LOD detach contract: Bottle / SkyrimLODFixes, see
// THIRD-PARTY.txt.
#include "PCH.hpp"

namespace hdn::gameplay {
namespace {
std::shared_ptr<spdlog::logger> pluginLogger;
InputGate gate; // accessed only by SKSE main-thread tasks
bool savedBlockInput{};
bool ownsBlockInput{};
bool enableInput{}, enableLOD{};
std::atomic<unsigned> inputMode{};
std::atomic<unsigned> inputEpoch{};
std::atomic<bool> pumpPending{};
std::atomic<int> lastLODToken{};
std::atomic<int> lodEpoch{};

void clearHeld(RE::PlayerControls& pc)
{
  pc.data.moveInputVec = {};
  pc.data.prevMoveVec = {};
  pc.data.lookInputVec = {};
  pc.data.prevLookVec = {};
  pc.data.autoMove = false;
  pc.data.setupHeldStatesForRelease = true;
  // SetHeldStateActive is a state assignment, not an animation/attack command.
  if (pc.sprintHandler)
    pc.sprintHandler->SetHeldStateActive(false);
  if (pc.runHandler)
    pc.runHandler->SetHeldStateActive(false);
  if (auto* attack = pc.attackBlockHandler) {
    attack->SetHeldStateActive(false);
    attack->GetRuntimeData().heldLeft = false;
    attack->GetRuntimeData().heldRight = false;
    attack->GetRuntimeData().heldTimeMs = 0;
  }
}
void clearDevices()
{
  auto* devices = RE::BSInputDeviceManager::GetSingleton();
  if (!devices)
    return;
  if (auto* keyboard = devices->GetKeyboard())
    keyboard->ClearInputState();
  if (auto* mouse = devices->GetMouse())
    mouse->ClearInputState();
  if (auto* gamepad = devices->GetGamepad())
    gamepad->ClearInputState();
}
bool physicallyNeutral()
{
  // Raw Win32 state keeps observing key-up while CEF owns DirectInput.
  // Keyboard layout/rebindings do not affect the neutral requirement.
  for (int key = 1; key < 256; ++key) {
    if (GetAsyncKeyState(key) & 0x8000)
      return false;
  }
  for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user) {
    XINPUT_STATE state{};
    if (XInputGetState(user, &state) != ERROR_SUCCESS)
      continue;
    const auto& g = state.Gamepad;
    if (g.wButtons || g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        std::abs(int(g.sThumbLX)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
        std::abs(int(g.sThumbLY)) > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
        std::abs(int(g.sThumbRX)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ||
        std::abs(int(g.sThumbRY)) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
      return false;
  }
  return true;
}
void restoreInput()
{
  if (auto* pc = RE::PlayerControls::GetSingleton(); pc && ownsBlockInput) {
    clearHeld(*pc);
    clearDevices();
    if (pc->blockPlayerInput)
      pc->blockPlayerInput = savedBlockInput;
  }
  ownsBlockInput = false;
  gate.mode = InputMode::Idle;
  inputMode = 0;
}
void pumpInput()
{
  auto* pc = RE::PlayerControls::GetSingleton();
  if (!pc || gate.mode == InputMode::Idle)
    return;
  clearHeld(*pc);
  if (gate.mode == InputMode::AwaitNeutral &&
      gate.neutral(physicallyNeutral())) {
    restoreInput();
    pluginLogger->info(
      "[input] resumed after neutral physical input; fresh press required");
  }
  inputMode = static_cast<unsigned>(gate.mode);
}
int apiVersion(RE::StaticFunctionTag*)
{
  return 1;
}
int getInputMode(RE::StaticFunctionTag*)
{
  return int(inputMode.load());
}
int getLastLODToken(RE::StaticFunctionTag*)
{
  return lastLODToken.load();
}
void setChatActive(RE::StaticFunctionTag*, bool active)
{
  if (!enableInput)
    return;
  const auto epoch = ++inputEpoch;
  SKSE::GetTaskInterface()->AddTask([active, epoch] {
    if (epoch != inputEpoch.load())
      return;
    auto* pc = RE::PlayerControls::GetSingleton();
    if (!pc)
      return;
    if (active) {
      if (!ownsBlockInput) {
        savedBlockInput = pc->blockPlayerInput;
        ownsBlockInput = true;
      }
      pc->blockPlayerInput = true;
      clearHeld(*pc);
      clearDevices();
      gate.open();
      pluginLogger->info(
        "[input] chat acquired; movement/held actions cleared");
    } else {
      gate.close();
      pluginLogger->info(
        "[input] chat released; awaiting neutral physical input");
    }
    inputMode = static_cast<unsigned>(gate.mode);
  });
}
void pumpChatInput(RE::StaticFunctionTag*)
{
  if (!enableInput || pumpPending.exchange(true))
    return;
  const auto epoch = inputEpoch.load();
  SKSE::GetTaskInterface()->AddTask([epoch] {
    pumpPending = false;
    if (epoch == inputEpoch.load())
      pumpInput();
  });
}
void resetChatInput(RE::StaticFunctionTag*)
{
  const auto epoch = ++inputEpoch;
  SKSE::GetTaskInterface()->AddTask([epoch] {
    if (epoch == inputEpoch.load())
      restoreInput();
  });
}
RE::BGSTerrainManager* managerFor(RE::TESWorldSpace* world)
{
  for (int hops = 0; world && hops < 16; ++hops) {
    if (!world->parentWorld ||
        !world->parentUseFlags.any(
          RE::TESWorldSpace::ParentUseFlag::kUseLODData))
      return world->terrainManager;
    world = world->parentWorld;
  }
  return nullptr;
}
void requestLODRefresh(RE::StaticFunctionTag*, int token, int worldID, float x,
                       float y, float z)
{
  if (!enableLOD || token <= 0 || worldID == 0 ||
      token == lastLODToken.load() || !validDestination(x, y, z))
    return;
  lodEpoch = token;
  // Capture only primitive IDs and coordinates. Re-resolve all engine objects.
  SKSE::GetTaskInterface()->AddTask([token, worldID, x, y] {
    if (lodEpoch.load() != token || lastLODToken.load() == token)
      return;
    auto* ui = RE::UI::GetSingleton();
    if (!ui || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
        ui->IsMenuOpen(RE::MapMenu::MENU_NAME))
      return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* world = player ? player->GetWorldspace() : nullptr;
    if (!world ||
        !atDestination(world->GetFormID(), player->GetPositionX(),
                       player->GetPositionY(), worldID, x, y))
      return;
    // Verified AE 1.6.1170 Address Library IDs, from SkyrimLODFixes.
    static REL::Relocation<RE::BGSTerrainManager**> active{ REL::ID(402262) };
    auto* manager = managerFor(world);
    if (!manager || manager != *active || manager->mapMode ||
        !manager->hasLOD || !manager->rootNode)
      return;
    using Detach = void (*)(RE::BGSTerrainManager*, char, char);
    static REL::Relocation<Detach> detach{ REL::ID(31812) };
    // Engine-owned teardown: clear initialization and all world/map LOD
    // handles.
    detach(manager, 1, 1);
    manager->staticDataLoaded = false;
    lastLODToken = token;
    pluginLogger->info("[lod] teleport {}: refreshed world {:x} cell {},{}",
                       token, worldID, cellOf(x), cellOf(y));
  });
}
bool registerPapyrus(RE::BSScript::IVirtualMachine* vm)
{
  vm->RegisterFunction("ApiVersion", "HdnGameplayFixes", apiVersion);
  vm->RegisterFunction("GetInputMode", "HdnGameplayFixes", getInputMode);
  vm->RegisterFunction("SetChatActive", "HdnGameplayFixes", setChatActive);
  vm->RegisterFunction("PumpChatInput", "HdnGameplayFixes", pumpChatInput);
  vm->RegisterFunction("ResetChatInput", "HdnGameplayFixes", resetChatInput);
  vm->RegisterFunction("GetLastLODToken", "HdnGameplayFixes", getLastLODToken);
  vm->RegisterFunction("RequestLODRefresh", "HdnGameplayFixes",
                       requestLODRefresh);
  return true;
}
void onMessage(SKSE::MessagingInterface::Message* message)
{
  if (message->type == SKSE::MessagingInterface::kPreLoadGame ||
      message->type == SKSE::MessagingInterface::kNewGame) {
    ++inputEpoch;
    ++lodEpoch;
    restoreInput();
    lastLODToken = 0;
  }
}
} // namespace
} // namespace hdn::gameplay

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
  using namespace hdn::gameplay;
  if (!skse)
    return false;
  const auto version = skse->RuntimeVersion();
  if (!supportedRuntime(version[0], version[1], version[2], version[3]))
    return false;
  const auto config =
    std::filesystem::absolute("Data/SKSE/Plugins/HdnGameplayFixes.ini");
  enableInput =
    GetPrivateProfileIntW(L"Fixes", L"ChatInput", 1, config.c_str()) != 0;
  enableLOD =
    GetPrivateProfileIntW(L"Fixes", L"TeleportLOD", 1, config.c_str()) != 0;
  if (!enableInput && !enableLOD)
    return true; // recovery even without Address Library
  const auto library = std::filesystem::path("Data/SKSE/Plugins") /
    ("versionlib-" + version.string() + ".bin");
  if (!std::filesystem::is_regular_file(library))
    return false;
  SKSE::Init(skse, SKSE::InitInfo{ .log = false });
  auto path = SKSE::log::log_directory();
  spdlog::sink_ptr sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  if (path) {
    *path /= "HdnGameplayFixes.log";
    sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(),
                                                               true);
  }
  pluginLogger =
    std::make_shared<spdlog::logger>("HdnGameplayFixes", std::move(sink));
  pluginLogger->set_level(spdlog::level::info);
  pluginLogger->flush_on(spdlog::level::info);
  pluginLogger->info("HDN gameplay fixes 0.1.0 runtime {}; chat={} lod={}",
                     version.string(), enableInput, enableLOD);
  return SKSE::GetPapyrusInterface()->Register(registerPapyrus) &&
    SKSE::GetMessagingInterface()->RegisterListener(onMessage);
}
