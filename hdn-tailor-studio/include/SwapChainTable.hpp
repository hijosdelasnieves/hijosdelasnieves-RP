#pragma once
#include <algorithm>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace hdn::studio {
// The game can retain IDXGISwapChain4. Copying only the 18 base entries
// would truncate later methods (ResizeBuffers1, SetHDRMetaData, etc.).
// QueryInterface also verifies which interfaces share THIS concrete pointer.
inline size_t swapChainTableEntries(IDXGISwapChain* swap)
{
  const auto count = [&]<class T>(size_t entries) {
    Microsoft::WRL::ComPtr<T> version;
    return SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&version))) &&
        static_cast<IDXGISwapChain*>(version.Get()) == swap
      ? entries
      : size_t{ 0 };
  };
  return (std::max)({ size_t{ 18 },
                      count.template operator()<IDXGISwapChain1>(29),
                      count.template operator()<IDXGISwapChain2>(36),
                      count.template operator()<IDXGISwapChain3>(40),
                      count.template operator()<IDXGISwapChain4>(41) });
}
}
