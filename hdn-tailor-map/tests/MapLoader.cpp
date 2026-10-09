#include <Windows.h>
#include <ShlObj.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// The real official DLL, real installed Address Library and exact SKSE ABI.
// Hook-site opcodes are synthetic: this is NOT a Skyrim/Scaleform render test.
#pragma section(".hdnimg", read, write)
__declspec(allocate(".hdnimg")) unsigned char imagePadding[64 * 1024 * 1024] = { 1 };
namespace {
int checks = 0;
int registrations = 0;
int dispatches = 0;
int allocations = 0;
void check(bool value)
{
  ++checks;
  if (!value) {
    std::cerr << "Map loader check failed: " << checks << '\n';
    std::exit(1);
  }
}
struct Message
{
  const char* sender;
  std::uint32_t type;
  std::uint32_t dataLen;
  void* data;
};
using Callback = void (*)(Message*);
Callback listener = nullptr;
bool registerListener(std::uint32_t, const char* sender, void* callback)
{
  ++registrations;
  if (sender && std::string(sender) == "InfinityUI")
    return false;
  check(sender && std::string(sender) == "SKSE");
  listener = reinterpret_cast<Callback>(callback);
  return true;
}
bool dispatch(std::uint32_t, std::uint32_t, void* data, std::uint32_t size, const char*)
{
  ++dispatches;
  check(data && size >= 16);
  return true;
}
void* eventDispatcher(std::uint32_t) { return nullptr; }
struct Messaging
{
  std::uint32_t version = 2;
  decltype(&registerListener) RegisterListener = registerListener;
  decltype(&dispatch) Dispatch = dispatch;
  decltype(&eventDispatcher) GetEventDispatcher = eventDispatcher;
} messaging;
void* allocate(std::uint32_t, std::size_t size)
{
  ++allocations;
  check(size <= 4096);
  DWORD previous = 0;
  auto buffer = imagePadding + 40 * 1024 * 1024;
  check(VirtualProtect(buffer, 4096, PAGE_EXECUTE_READWRITE, &previous) != 0);
  return buffer;
}
struct Trampoline
{
  std::uint32_t version = 1;
  decltype(&allocate) Branch = allocate;
  decltype(&allocate) Local = allocate;
} trampoline;
void* query(std::uint32_t id)
{
  if (id == 5)
    return &messaging;
  if (id == 7)
    return &trampoline;
  return nullptr;
}
std::uint32_t pluginHandle() { return 1; }
std::uint32_t releaseIndex() { return 22; }
const void* pluginInfo(const char*) { return nullptr; }
constexpr std::uint32_t pack(unsigned major, unsigned minor, unsigned patch)
{
  return (major << 24) | (minor << 16) | (patch << 4);
}
struct LoadInterface
{
  std::uint32_t skseVersion = pack(2, 2, 6);
  std::uint32_t runtimeVersion = pack(1, 6, 1170);
  std::uint32_t editorVersion = 0;
  std::uint32_t isEditor = 0;
  decltype(&query) QueryInterface = query;
  decltype(&pluginHandle) GetPluginHandle = pluginHandle;
  decltype(&releaseIndex) GetReleaseIndex = releaseIndex;
  decltype(&pluginInfo) GetPluginInfo = pluginInfo;
};
unsigned char* image(std::size_t rva)
{
  auto address = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)) + rva;
  check(address >= imagePadding && address + 1024 < imagePadding + sizeof(imagePadding));
  return address;
}
void callSite(unsigned char* address)
{
  address[0] = 0xE8;
  const std::int32_t displacement = 0x1000;
  std::memcpy(address + 1, &displacement, sizeof(displacement));
}
}
int main(int argc, char** argv)
{
  check(argc == 3);
  const std::string mode(argv[2]);
  const auto dllPath = std::filesystem::absolute(argv[1]);
  const auto loaded = LoadLibraryW(dllPath.c_str());
  check(loaded != nullptr);
  auto metadata = reinterpret_cast<const std::uint32_t*>(GetProcAddress(loaded, "SKSEPlugin_Version"));
  using Load = bool (*)(const LoadInterface*);
  auto load = reinterpret_cast<Load>(GetProcAddress(loaded, "SKSEPlugin_Load"));
  if (mode == "neutral") {
    check(!metadata && !load && !GetProcAddress(loaded, "SKSEPlugin_Query"));
    check(FreeLibrary(loaded) != 0);
    std::cout << checks << " neutral recovery DLL checks PASS\n";
    return 0;
  }
  check(metadata && load);
  check(metadata[0] == 1 && metadata[1] == pack(2, 2, 1));
  check(std::string(reinterpret_cast<const char*>(metadata + 2)) == "MapMarkerFramework");
  check(metadata[0x308 / 4] == 5); // Address Library + AE independence flags.
  check(metadata[0x304 / 4] == 0);
  for (unsigned i = 0; i < 16; ++i)
    check(metadata[(0x30c / 4) + i] == 0);
  auto hud = image(0x91c250 + (mode == "alt-hud" ? 0xff : 0xfe));
  auto map = image(0x9836c0 + 0x1d1);
  auto local = image(0x97c3e0 + 0x941);
  auto music = image(0x921280 + 0x2bb);
  if (mode != "rejected-hooks") {
    callSite(hud);
    callSite(map);
    callSite(local);
  }
  // Upstream only guards movie/local hooks; discovery patch always installs.
  wchar_t* documents = nullptr;
  check(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents) == S_OK);
  std::filesystem::create_directories(std::filesystem::path(documents) /
    "My Games/Skyrim Special Edition/SKSE");
  CoTaskMemFree(documents);
  std::filesystem::create_directories("Data/SKSE/Plugins");
  {
    std::ofstream ini("Data/SKSE/Plugins/MapMarkerFramework.ini");
    ini << "[Resources]\nsResourceFile=resources\\hdn-tailor-art.swf\n";
  }
  LoadInterface fixture;
  check(load(&fixture));
  check(allocations == 1 && registrations == 1 && listener);
  check(music[0] != 0);
  if (mode == "rejected-hooks") {
    check(hud[0] == 0 && map[0] == 0 && local[0] == 0);
  } else {
    check(hud[0] == 0xE8 && map[0] == 0xE8 && local[0] == 0xE8);
    check(hud[1] != 0 || hud[2] != 0 || hud[3] != 0);
  }
  Message postLoad{ "SKSE", 0, 0, nullptr };
  listener(&postLoad);
  check(registrations == 2 && dispatches == 0);
  // Actual DataLoaded callback: empty engine singleton is synthetic and null.
  Message dataLoaded{ "SKSE", 8, 0, nullptr };
  listener(&dataLoaded);
  check(dispatches == 1);
  check(FreeLibrary(loaded) != 0);
  std::cout << checks << " official DLL + SKSE2.2.6 / runtime1170 / real Address Library checks PASS (" << mode << ")\n";
  std::cout << "Synthetic hooks/empty engine, NOT a Skyrim rendering acceptance test\n";
}
