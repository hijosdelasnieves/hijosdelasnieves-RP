#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// ABI fixture for SKSE 2.2.6. No Skyrim executable, Address Library or SDK
// initialization is permitted here. This tests the REAL generated DLL,
// not just a runtime-number policy or a mocked JavaScript adapter.
namespace {
int checks = 0;
int interfaceCalls = 0;
void check(bool value)
{
  ++checks;
  if (!value) {
    std::cerr << "Loader check failed: " << checks << '\n';
    std::exit(1);
  }
}
constexpr std::uint32_t pack(unsigned major, unsigned minor, unsigned patch)
{
  return (major << 24) | (minor << 16) | (patch << 4);
}
void* query(std::uint32_t)
{
  ++interfaceCalls;
  return nullptr;
}
struct LoaderInterface
{
  std::uint32_t skseVersion = pack(2, 2, 6);
  std::uint32_t runtimeVersion = pack(1, 6, 1170);
  std::uint32_t editorVersion = 0;
  std::uint32_t isEditor = 0;
  void* (*QueryInterface)(std::uint32_t) = query;
  std::uint32_t (*GetPluginHandle)() = nullptr;
  std::uint32_t (*GetReleaseIndex)() = nullptr;
  const void* (*GetPluginInfo)(const char*) = nullptr;
};
struct PluginInfo
{
  std::uint32_t version;
  const char* name;
  std::uint32_t pluginVersion;
};
}

int main(int argc, char** argv)
{
  check(argc == 3 || argc == 4);
  const auto dllPath = std::filesystem::absolute(argv[1]);
  const auto previous = std::filesystem::current_path();
  const auto sandbox = std::filesystem::temp_directory_path() /
    ("hdn-studio-loader-" + std::to_string(GetCurrentProcessId()));
  check(!std::filesystem::exists(sandbox));
  std::filesystem::create_directories(sandbox / "Data/SKSE/Plugins");
  std::filesystem::current_path(sandbox);
  const std::string mode(argv[2]);
  const bool missingLibrary = mode == "missing-library";
  const bool isolatedMissing = mode == "isolated-missing-library";
  const bool quarantined = mode == "quarantined" || mode == "quarantined-crlf";
  check(missingLibrary || isolatedMissing || quarantined ||
        mode == "disabled");
  {
    std::ofstream config("Data/SKSE/Plugins/HdnTailorStudio.ini",
                         std::ios::binary);
    const auto newline = mode == "quarantined-crlf" ? "\r\n" : "\n";
    config << "[Studio]" << newline
           << "EnableExperimental=" << (missingLibrary || quarantined ? 1 : 0)
           << newline
           << "EnableIsolatedMeshBackend=" << (isolatedMissing ? 1 : 0)
           << newline;
  }
  if (quarantined) {
    // Existence alone was the old loader guard. A corrupt database must NOT
    // be opened: a quarantined backend must not initialize CommonLib at all.
    for (const auto* name :
         { "versionlib-1-6-1170-0.bin", "version-1-5-97-0.bin" }) {
      std::ofstream library(std::filesystem::path("Data/SKSE/Plugins") / name,
                            std::ios::binary);
      library << "invalid database: must never be opened";
      check(library.good());
    }
  }
  const auto module = LoadLibraryW(dllPath.c_str());
  check(module != nullptr);
  using Load = bool (*)(const LoaderInterface*);
  using Query = bool (*)(const void*, PluginInfo*);
  const auto load =
    reinterpret_cast<Load>(GetProcAddress(module, "SKSEPlugin_Load"));
  const auto pluginQuery =
    reinterpret_cast<Query>(GetProcAddress(module, "SKSEPlugin_Query"));
  const auto metadata = reinterpret_cast<const std::uint32_t*>(
    GetProcAddress(module, "SKSEPlugin_Version"));
  check(load && pluginQuery && metadata);
  check(metadata[0] == 1);
  check(metadata[1] == pack(0, 2, 1));
  const auto versions = metadata + 0x30C / sizeof(std::uint32_t);
  check(versions[0] == pack(1, 5, 97));
  check(versions[1] == pack(1, 6, 1170));
  for (unsigned index = 2; index < 16; ++index)
    check(versions[index] == 0);
  // Exact runtime declarations, not "all AE versions" or "no struct use".
  check(metadata[0x308 / 4] == 0);
  check((metadata[0x304 / 4] & 1) == 0);
  PluginInfo info{};
  check(pluginQuery(nullptr, &info));
  check(info.version == 1 &&
        std::string(info.name) == (argc == 4 ? argv[3] : "HdnTailorStudio"));
  check(info.pluginVersion == pack(0, 2, 1));
  check(!load(nullptr));
  LoaderInterface fixture;
  check(load(&fixture)); // The version which previously failed in game.
  fixture.runtimeVersion = pack(1, 5, 97);
  check(load(&fixture));
  for (auto version : { pack(1, 6, 1130), pack(1, 6, 1179), pack(1, 7, 104),
                        pack(1, 4, 15) }) {
    fixture.runtimeVersion = version;
    check(!load(&fixture));
  }
  check(interfaceCalls == 0);
  check(FreeLibrary(module));
  std::filesystem::current_path(previous);
  std::filesystem::remove_all(sandbox); // Only this freshly-created fixture.
  std::cout << checks << " real DLL loader checks passed: " << argv[2]
            << " (not a Skyrim render test)\n";
}
