#include "BackendSafety.hpp"
#include "PCH.hpp"
#include "Runtime.hpp"
#include "Studio.hpp"
#include <spdlog/sinks/null_sink.h>

namespace hdn::studio {
namespace {
std::shared_ptr<spdlog::logger> pluginLogger;
}
spdlog::logger& logger()
{
  return *pluginLogger;
}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
  if (!skse)
    return false;
  const auto runtime = skse->RuntimeVersion();
  if (!hdn::studio::supportedRuntime(
        { runtime[0], runtime[1], runtime[2], runtime[3] }))
    return false;
  const auto config =
    std::filesystem::absolute("Data/SKSE/Plugins/HdnTailorStudio.ini");
  // Disabled recovery must load successfully WITHOUT initializing CommonLib,
  // Papyrus, the address database or any game renderer. Launcher overlays do
  // not delete stale DLLs, so re-publishing the old ZIP cannot recover load.
  if (GetPrivateProfileIntW(L"Studio", L"EnableExperimental", 0,
                            config.c_str()) != 1) {
    OutputDebugStringA(
      "HDN tailor 0.1.3: disabled; legacy preview retained\n");
    return true;
  }
  // Deliberately before Address Library, SKSE::Init, Papyrus and Present.
  // Even an old EnableExperimental=1 file cannot reactivate the CTD path.
  if (!hdn::studio::privateBackendValidated) {
    OutputDebugStringA("HDN tailor 0.1.3: unsafe private backend quarantined; "
                       "legacy preview retained\n");
    return true;
  }
  const auto library = std::filesystem::path("Data/SKSE/Plugins") /
    ((runtime == SKSE::RUNTIME_SSE_1_5_97 ? "version-" : "versionlib-") +
     runtime.string() + ".bin");
  if (!std::filesystem::is_regular_file(library)) {
    OutputDebugStringA("HDN tailor 0.1.3: Address Library missing; legacy "
                       "preview retained\n");
    return true;
  }
  SKSE::Init(skse, SKSE::InitInfo{ .log = false });
  auto path = SKSE::log::log_directory();
  spdlog::sink_ptr sink = std::make_shared<spdlog::sinks::null_sink_mt>();
  if (path) {
    *path /= "HdnTailorStudio.log";
    sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(),
                                                               true);
  }
  hdn::studio::pluginLogger =
    std::make_shared<spdlog::logger>("HdnTailorStudio", std::move(sink));
  hdn::studio::logger().set_level(spdlog::level::info);
  hdn::studio::logger().flush_on(spdlog::level::info);
  // Do not replace the shared spdlog default logger used by other plugins.
  hdn::studio::logger().info(
    "Experimental DEV studio 0.1.3; runtime={}; does not replace "
    "SkyrimPlatform",
    runtime.string());
  return SKSE::GetPapyrusInterface()->Register(hdn::studio::registerPapyrus) &&
    SKSE::GetMessagingInterface()->RegisterListener(hdn::studio::onMessage);
}
