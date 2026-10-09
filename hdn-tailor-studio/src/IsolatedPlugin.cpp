// HDN isolated studio, GPL-3.0. Never activates the quarantined Clone backend.
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
  // New opt-in is distinct. Old EnableExperimental=1 NEVER enables this build
  // or the crash path. Recovery loads both DLLs without touching the engine.
  if (GetPrivateProfileIntW(L"Studio", L"EnableIsolatedMeshBackend", 0,
                            config.c_str()) != 1)
    return true;
  const auto library = std::filesystem::path("Data/SKSE/Plugins") /
    ((runtime == SKSE::RUNTIME_SSE_1_5_97 ? "version-" : "versionlib-") +
     runtime.string() + ".bin");
  if (!std::filesystem::is_regular_file(library))
    return true;
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
  hdn::studio::logger().info("Experimental isolated studio 0.2.3; runtime={}; "
                             "API=2; lazy startup; no NiClone, "
                             "no inventory renderer, SkyrimPlatform unchanged",
                             runtime.string());
  return SKSE::GetPapyrusInterface()->Register(hdn::studio::registerPapyrus) &&
    SKSE::GetMessagingInterface()->RegisterListener(hdn::studio::onMessage);
}
