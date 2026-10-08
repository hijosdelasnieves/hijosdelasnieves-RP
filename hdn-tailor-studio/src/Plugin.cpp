#include "PCH.hpp"
#include "Studio.hpp"

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
  SKSE::Init(skse);
  auto path = SKSE::log::log_directory();
  if (path) {
    *path /= "HdnTailorStudio.log";
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
    auto logger = std::make_shared<spdlog::logger>("HdnTailorStudio", std::move(sink));
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
  }
  SKSE::log::info("Experimental DEV studio 0.1.0; does not replace SkyrimPlatform");
  return SKSE::GetPapyrusInterface()->Register(hdn::studio::registerPapyrus) &&
    SKSE::GetMessagingInterface()->RegisterListener(hdn::studio::onMessage);
}
