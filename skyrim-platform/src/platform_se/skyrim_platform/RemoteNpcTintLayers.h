#pragma once

#include <algorithm>
#include <cstdint>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// Pure CPU preparation. Installed arrays belong to TESNPC and are immutable;
// callers must configure a fresh appearance base BEFORE placing any actor.
// No TintMask/TESTexture, render texture, shader or GPU object is created here.
namespace Hdn {
constexpr std::size_t kRemoteFaceMaxMasks = 128;
constexpr std::size_t kRemoteFaceMaxAssets = 512;

struct RemoteFaceMask
{
  std::int32_t type = 0;
  std::uint32_t argb = 0;
  std::string texturePath;
  bool operator==(const RemoteFaceMask&) const = default;
};

struct RemoteFaceAsset
{
  std::int32_t type = 0;
  std::uint16_t tintIndex = 0;
  std::string texturePath;
  std::vector<std::pair<std::uint16_t, std::uint32_t>> palette;
};

struct RemoteFaceLayer
{
  std::uint32_t argb = 0;
  std::uint16_t tintIndex = 0;
  std::uint16_t preset = 0;
  std::uint16_t interpolationValue = 0;
};

struct RemoteFaceLayerPlan
{
  bool valid = false;
  std::vector<RemoteFaceLayer> layers;
  std::size_t matchedCount = 0;
  std::size_t missingCount = 0;
  std::string reason;
};

// CreateNpc is a memcpy clone. Detach borrowed pointers even when allocation
// fails, so a discarded dynamic form cannot later destroy its template's data.
template <class Npc, class AllocateHead>
bool IsolateRemoteAppearanceData(Npc* npc, AllocateHead allocateHead)
{
  if (!npc) return false;
  const auto* borrowed = npc->headRelatedData;
  npc->headRelatedData = nullptr;
  npc->tintLayers = nullptr;
  auto* isolated = allocateHead();
  if (!isolated) return false;
  if (borrowed) *isolated = *borrowed;
  else *isolated = {};
  npc->headRelatedData = isolated;
  return true;
}

inline std::string NormalizeRemoteTintPath(const std::string& source)
{
  if (source.empty() || source.size() > 1024) return {};
  std::string path;
  path.reserve(source.size());
  for (const unsigned char value : source) {
    if (value < 0x20 || value == ':' || value == 0x7f) return {};
    const char ch = value == '/' ? '\\' :
      (value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) :
                                    static_cast<char>(value));
    if (ch == '\\' && (path.empty() || path.back() == '\\')) continue;
    path.push_back(ch);
  }
  if (path.starts_with("data\\")) path.erase(0, 5);
  if (path.starts_with("textures\\")) path.erase(0, 9);
  if (path.empty() || path.ends_with('\\') || path == ".." ||
      path.starts_with("..\\") || path.ends_with("\\..") ||
      path.find("\\..\\") != std::string::npos) return {};
  return path;
}

inline RemoteFaceLayerPlan PrepareRemoteFaceLayers(
  const std::vector<RemoteFaceMask>& masks,
  const std::vector<RemoteFaceAsset>& assets)
{
  RemoteFaceLayerPlan result;
  if (masks.empty() || masks.size() > kRemoteFaceMaxMasks ||
      assets.empty() || assets.size() > kRemoteFaceMaxAssets) {
    result.reason = "invalid-layer-count";
    return result;
  }
  std::unordered_set<std::uint16_t> used;
  bool requestedSkin = false, matchedSkin = false;
  for (const auto& mask : masks) {
    const auto path = NormalizeRemoteTintPath(mask.texturePath);
    if (mask.type < 0 || mask.type > 14 || path.empty()) {
      result.reason = "invalid-tint-mask";
      return result;
    }
    requestedSkin = requestedSkin || mask.type == 6;
    const RemoteFaceAsset* match = nullptr;
    for (const auto& asset : assets) {
      if (asset.type != mask.type || NormalizeRemoteTintPath(asset.texturePath) != path) continue;
      if (match && match->tintIndex != asset.tintIndex) {
        result.reason = "ambiguous-race-tint";
        return result;
      }
      match = &asset;
    }
    if (!match) { ++result.missingCount; continue; }
    if (!used.insert(match->tintIndex).second) {
      result.reason = "duplicate-tint-index";
      return result;
    }
    RemoteFaceLayer layer;
    layer.argb = mask.argb;
    layer.tintIndex = match->tintIndex;
    // TINC stores the actual colour. TIAS retains the matching race palette
    // index if present; custom colours use the ordinary default index0.
    for (const auto& [index, rgb] : match->palette) {
      if ((rgb & 0xffffffu) == (mask.argb & 0xffffffu)) { layer.preset = index; break; }
    }
    const auto alpha = (mask.argb >> 24) & 0xffu;
    layer.interpolationValue = static_cast<std::uint16_t>((alpha * 100u + 127u) / 255u);
    result.layers.push_back(layer);
    ++result.matchedCount;
    matchedSkin = matchedSkin || mask.type == 6;
  }
  if (result.layers.empty() || (requestedSkin && !matchedSkin)) {
    result.reason = requestedSkin ? "skin-tone-not-in-race" : "no-matched-tints";
    return result;
  }
  result.valid = true;
  return result;
}

// Allocate everything while unpublished. Any exception/partial allocation
// releases ONLY components allocated by this call. Once assigned, ownership
// is transferred to TESNPC; no registry/shared_ptr also deletes those objects.
// The pointer held by the old template is never freed or modified.
template <class Npc, class Array, class Layer, class AllocateArray,
          class AllocateLayer, class DestroyArray, class DestroyLayer>
bool InstallImmutableRemoteLayers(
  Npc* npc, const std::vector<RemoteFaceLayer>& recipes,
  AllocateArray allocateArray, AllocateLayer allocateLayer,
  DestroyArray destroyArray, DestroyLayer destroyLayer)
{
  if (!npc || npc->tintLayers || recipes.empty()) return false;
  Array* array = nullptr;
  std::vector<Layer*> owned;
  try {
    owned.reserve(recipes.size());
    array = allocateArray();
    if (!array) return false;
    array->reserve(static_cast<std::uint32_t>(recipes.size()));
    for (const auto& recipe : recipes) {
      auto* layer = allocateLayer(recipe);
      if (!layer) throw std::bad_alloc();
      owned.push_back(layer);
      array->push_back(layer);
    }
    if (array->size() != recipes.size()) throw std::bad_alloc();
    npc->tintLayers = array;
    return true;
  } catch (...) {
    for (auto* layer : owned) destroyLayer(layer);
    if (array) destroyArray(array);
    return false;
  }
}
}
