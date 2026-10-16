#pragma once

#include <atomic>
#include <cstdint>

// HDN 16/10/2026: contadores de la capa (CEF + textos) que se pinta en el
// gancho de Present. Tras un Alt+Tab desaparecía toda la capa a la vez
// (interfaces, nombres, iconos) sin nada raro en el JS; estos contadores dicen
// qué le pasa al pintado y se leen desde el JS con getOverlayStats().
namespace CEFUtils {
struct OverlayStats
{
  std::atomic<uint64_t> presents{ 0 };      // llamadas a nuestro gancho de Present
  std::atomic<uint64_t> renders{ 0 };       // capas pintadas
  std::atomic<uint64_t> boundOther{ 0 };    // el juego tenía otro destino puesto al presentar
  std::atomic<uint64_t> noBound{ 0 };       // el juego no tenía ningún destino puesto
  std::atomic<uint64_t> viewportOther{ 0 }; // el área puesta no era la pantalla entera
  std::atomic<uint64_t> deviceChanges{ 0 }; // la cadena de presentación cambió de dispositivo
  std::atomic<uint64_t> noBackBuffer{ 0 };  // no se pudo coger el búfer de la pantalla
  std::atomic<uint64_t> rearms{ 0 };        // veces que se volvió a enganchar Present
  std::atomic<uint32_t> backWidth{ 0 };
  std::atomic<uint32_t> backHeight{ 0 };
  std::atomic<uint32_t> boundWidth{ 0 };
  std::atomic<uint32_t> boundHeight{ 0 };
  std::atomic<uint32_t> viewportWidth{ 0 };
  std::atomic<uint32_t> viewportHeight{ 0 };
};

inline OverlayStats& GetOverlayStats() noexcept
{
  static OverlayStats s_stats;
  return s_stats;
}
}
