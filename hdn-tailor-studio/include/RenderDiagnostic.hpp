#pragma once
#include <cstdint>
#include <string_view>

namespace hdn::studio {
enum class RenderReason
{
  none,
  missingUI,
  pausedUI,
  itemMenu,
  customRendering,
  closingMenus,
  missingCamera,
  missingInventory,
  loadTask,
  loadedModels,
  temporaryReference,
  lightSchemes,
  menuObjects,
  swapDevice,
  swapBuffer,
  renderTarget,
  surface,
  frustum,
  noGeometry
};
struct SceneState
{
  bool ui = false, camera = false, inventory = false;
  std::uint32_t pauses = 0, itemMenus = 0, customRendering = 0;
  bool closing = false, loadTask = false, temporaryReference = false;
  std::uint32_t loadedModels = 0, lightSchemes = 0, menuObjects = 0;
};
inline RenderReason sceneReason(const SceneState& state)
{
  if (!state.ui)
    return RenderReason::missingUI;
  if (state.pauses)
    return RenderReason::pausedUI;
  if (state.itemMenus)
    return RenderReason::itemMenu;
  if (state.customRendering)
    return RenderReason::customRendering;
  if (state.closing)
    return RenderReason::closingMenus;
  if (!state.camera)
    return RenderReason::missingCamera;
  if (!state.inventory)
    return RenderReason::missingInventory;
  if (state.loadTask)
    return RenderReason::loadTask;
  if (state.loadedModels)
    return RenderReason::loadedModels;
  if (state.temporaryReference)
    return RenderReason::temporaryReference;
  if (state.lightSchemes)
    return RenderReason::lightSchemes;
  if (state.menuObjects)
    return RenderReason::menuObjects;
  return RenderReason::none;
}
inline std::string_view reasonName(RenderReason reason)
{
  switch (reason) {
    case RenderReason::none:
      return "none";
    case RenderReason::missingUI:
      return "missing_ui";
    case RenderReason::pausedUI:
      return "ui_paused";
    case RenderReason::itemMenu:
      return "ui_item_menu";
    case RenderReason::customRendering:
      return "ui_custom_rendering";
    case RenderReason::closingMenus:
      return "ui_closing_menus";
    case RenderReason::missingCamera:
      return "missing_scene_camera";
    case RenderReason::missingInventory:
      return "missing_inventory_renderer";
    case RenderReason::loadTask:
      return "inventory_load_task";
    case RenderReason::loadedModels:
      return "inventory_loaded_models";
    case RenderReason::temporaryReference:
      return "inventory_temp_ref";
    case RenderReason::lightSchemes:
      return "scene_light_schemes";
    case RenderReason::menuObjects:
      return "scene_menu_objects";
    case RenderReason::swapDevice:
      return "d3d_get_device";
    case RenderReason::swapBuffer:
      return "d3d_get_buffer";
    case RenderReason::renderTarget:
      return "d3d_create_target";
    case RenderReason::surface:
      return "unsupported_surface";
    case RenderReason::frustum:
      return "invalid_frustum";
    case RenderReason::noGeometry:
      return "no_geometry_pixels";
  }
  return "unknown";
}
}
