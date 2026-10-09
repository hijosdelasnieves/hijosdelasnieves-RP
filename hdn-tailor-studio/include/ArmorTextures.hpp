#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hdn::studio {
// Value-only snapshot. Populated by the capture task on Skyrim's main thread;
// no TESForm, texture-set or engine string survives into Present.
struct ArmorTextureOverride
{
  std::string model;
  std::string shape;
  std::uint32_t index = 0;
  std::array<std::string, 8> textures;
};
inline std::string textureKey(std::string_view value, bool model = false)
{
  std::string result(value);
  for (auto& c : result) {
    if (c == '\\')
      c = '/';
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  if (model) {
    if (result.starts_with("data/"))
      result.erase(0, 5);
    if (result.starts_with("meshes/"))
      result.erase(0, 7);
  }
  return result;
}
inline thread_local const std::vector<ArmorTextureOverride>* armorTextures =
  nullptr;
struct ArmorTextureScope
{
  const std::vector<ArmorTextureOverride>* previous = armorTextures;
  explicit ArmorTextureScope(const std::vector<ArmorTextureOverride>& values)
  {
    armorTextures = &values;
  }
  ~ArmorTextureScope() { armorTextures = previous; }
  ArmorTextureScope(const ArmorTextureScope&) = delete;
  ArmorTextureScope& operator=(const ArmorTextureScope&) = delete;
};
inline const ArmorTextureOverride* findArmorTexture(
  std::string_view model, std::string_view shape, std::size_t index,
  const std::vector<std::string>& shapeNames)
{
  if (!armorTextures)
    return nullptr;
  const auto modelKey = textureKey(model, true);
  const auto shapeKey = textureKey(shape);
  if (modelKey.empty() || shapeKey.empty())
    return nullptr;
  const auto duplicates =
    std::count(shapeNames.begin(), shapeNames.end(), shapeKey);
  for (const auto& value : *armorTextures) {
    // An index is NOT a NIF block ID. Prefer an unambiguous named geometry.
    // Some shipped mods renamed the NIF shape without updating MO2S/MO3S
    // (e.g. hooded -> main, robe -> robe5). Their valid geometry ordinal still
    // identifies the target. Never use that fallback across components or when
    // a named geometry exists elsewhere; never use filtered part indices.
    const auto named =
      std::count(shapeNames.begin(), shapeNames.end(), value.shape);
    const bool sameName = value.shape == shapeKey &&
      (duplicates == 1 || (duplicates > 1 && value.index == index));
    const bool renamed = named == 0 && value.index == index &&
      index < shapeNames.size() && shapeNames[index] == shapeKey;
    if (value.model == modelKey && (sameName || renamed))
      return &value;
  }
  return nullptr;
}
template <class Paths>
inline void applyArmorTexture(Paths& paths, const ArmorTextureOverride& value)
{
  // BGSTextureSet order != BSLightingShader/NIF order. TX02 is environment
  // mask, NOT glow; TX03 is glow, NOT height. Slot 7 is remapped to specular
  // later by the original loader for model-space-normal materials.
  constexpr std::array<std::size_t, 8> nifSlots{ 0, 1, 5, 2, 3, 4, 6, 7 };
  for (std::size_t i = 0; i < nifSlots.size(); ++i)
    paths[nifSlots[i]] = value.textures[i];
  // Empty TXST entries replace old maps too: never retain the previous colour
  // variant's mask/glow/environment. Alpha, UVs and shader flags are
  // untouched.
}
}
