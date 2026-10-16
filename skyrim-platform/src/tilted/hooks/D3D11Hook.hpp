#pragma once

#include <Signal.hpp>

struct IDXGISwapChain;

namespace CEFUtils {
struct D3D11Hook
{
  TP_NOCOPYMOVE(D3D11Hook);

  Signal<void(IDXGISwapChain*)> OnCreate;
  Signal<void(IDXGISwapChain*)> OnPresent;
  Signal<void(IDXGISwapChain*)> OnLost;

  static void Install() noexcept;
  static D3D11Hook& Get() noexcept;

  // HDN 16/10/2026: se llama en cada fotograma de la lógica del juego. Si
  // nuestro gancho de Present lleva un rato sin llamarse mientras el juego
  // sigue y la entrada de la tabla ya no es la nuestra (otro programa la ha
  // sustituido sin pasar por nosotros), se vuelve a enganchar encadenando el
  // suyo. Si el otro programa nos llama a nosotros, no se toca nada.
  static void EnsurePresentHook() noexcept;

private:
  D3D11Hook() noexcept;
  ~D3D11Hook() = default;
};
}
