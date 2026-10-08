#include "PCH.hpp"
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
  if (skse->RuntimeVersion() != SKSE::RUNTIME_SSE_1_5_97)
    return false;
  SKSE::Init(skse);
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
    "Experimental DEV studio 0.1.0; does not replace SkyrimPlatform");
  return SKSE::GetPapyrusInterface()->Register(hdn::studio::registerPapyrus) &&
    SKSE::GetMessagingInterface()->RegisterListener(hdn::studio::onMessage);
}
