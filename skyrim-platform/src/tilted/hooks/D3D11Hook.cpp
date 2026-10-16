#include <D3D11Hook.hpp>

#include <d3d11.h>

#include <FunctionHook.hpp>
#include <OverlayStats.hpp>
#include <atomic>
#include <mutex>

using TDXGISwapChainPresent = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain* This,
                                                          UINT SyncInterval,
                                                          UINT Flags);

using TD3D11CreateDeviceAndSwapChain = HRESULT (*)(
  IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
  UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
  UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
  IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
  D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext);

TD3D11CreateDeviceAndSwapChain RealD3D11CreateDeviceAndSwapChain = nullptr;
TDXGISwapChainPresent RealDXGISwapChainPresent = nullptr;
// HDN 16/10/2026: tabla virtual cuya entrada de Present está enganchada (memoria
// fija de dxgi.dll o del envoltorio que la creó; no un objeto que se libere).
static std::atomic<uintptr_t*> g_presentVTable{ nullptr };
// La función que había en la tabla cuando enganchamos por primera vez.
static TDXGISwapChainPresent g_firstRealPresent = nullptr;
// Profundidad de nuestro Present en este hilo: si otro gancho nos vuelve a
// llamar desde dentro (cadena circular tras reenganchar), se va directo a la
// función que había al principio y no se pinta dos veces.
static thread_local int t_presentDepth = 0;

namespace CEFUtils {
HRESULT __stdcall HookDXGISwapChainPresent(IDXGISwapChain* This,
                                           UINT SyncInterval, UINT Flags)
{
  if (t_presentDepth > 0 && g_firstRealPresent)
    return g_firstRealPresent(This, SyncInterval, Flags);
  struct Depth
  {
    Depth() { ++t_presentDepth; }
    ~Depth() { --t_presentDepth; }
  } depth;

  auto& d3d11 = D3D11Hook::Get();

  GetOverlayStats().presents.fetch_add(1, std::memory_order_relaxed);

  static std::once_flag s_initializer;
  std::call_once(s_initializer, [&d3d11, This]() { d3d11.OnCreate(This); });

  d3d11.OnPresent(This);

  const auto result = RealDXGISwapChainPresent(This, SyncInterval, Flags);
  if (result == DXGI_ERROR_DEVICE_REMOVED ||
      result == DXGI_ERROR_DEVICE_RESET) {
    d3d11.OnLost(This);
  }

  return result;
}

HRESULT HookD3D11CreateDeviceAndSwapChain(
  _In_opt_ IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType,
  HMODULE Software, UINT Flags,
  _In_opt_ const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
  UINT SDKVersion, _In_opt_ const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
  _Out_opt_ IDXGISwapChain** ppSwapChain, _Out_opt_ ID3D11Device** ppDevice,
  _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
  _Out_opt_ ID3D11DeviceContext** ppImmediateContext)
{
  const auto result = RealD3D11CreateDeviceAndSwapChain(
    pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
    SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel,
    ppImmediateContext);

  if (RealDXGISwapChainPresent == nullptr && ppSwapChain && *ppSwapChain) {
    g_presentVTable.store(*reinterpret_cast<uintptr_t**>(*ppSwapChain));
    RealDXGISwapChainPresent =
      HookVTable(*ppSwapChain, 8, &HookDXGISwapChainPresent);
    g_firstRealPresent = RealDXGISwapChainPresent;
  }

  return result;
}

D3D11Hook::D3D11Hook() noexcept
{
}

void D3D11Hook::Install() noexcept
{
  TP_HOOK_IAT(D3D11CreateDeviceAndSwapChain, "d3d11.dll");
}

// Lectura protegida de una entrada de la tabla (sin objetos con destructor:
// __try no admite desenrollado en la misma función).
static bool ReadVTableSlot(uintptr_t* pVTable, size_t aIndex,
                           uintptr_t& aValue) noexcept
{
  __try {
    aValue = pVTable[aIndex];
    return true;
  } __except (1) {
    return false;
  }
}

void D3D11Hook::EnsurePresentHook() noexcept
{
  static uint64_t s_lastPresents = 0;
  static uint32_t s_quietTicks = 0;

  auto* pVTable = g_presentVTable.load(std::memory_order_relaxed);
  if (!pVTable || !RealDXGISwapChainPresent)
    return;

  const auto presents =
    GetOverlayStats().presents.load(std::memory_order_relaxed);
  if (presents != s_lastPresents) {
    s_lastPresents = presents;
    s_quietTicks = 0;
    return;
  }
  // ~0,5 s de lógica sin pasar por nuestro Present.
  if (++s_quietTicks < 30)
    return;
  s_quietTicks = 0;

  uintptr_t current = 0;
  if (!ReadVTableSlot(pVTable, 8, current) || !current)
    return;
  const auto ours = reinterpret_cast<uintptr_t>(&HookDXGISwapChainPresent);
  if (current == ours)
    return; // seguimos enganchados: el juego, simplemente, no presenta

  // Otro programa ha puesto su función sin pasar por la nuestra: la nuestra
  // vuelve a la tabla y llama a la suya, que sigue hasta la original.
  RealDXGISwapChainPresent = reinterpret_cast<TDXGISwapChainPresent>(current);
  const ProcessMemory memory(&pVTable[8], sizeof(&pVTable[8]));
  memory.Write(&HookDXGISwapChainPresent);
  GetOverlayStats().rearms.fetch_add(1, std::memory_order_relaxed);
}

D3D11Hook& D3D11Hook::Get() noexcept
{
  static D3D11Hook s_instance;
  return s_instance;
}
}
