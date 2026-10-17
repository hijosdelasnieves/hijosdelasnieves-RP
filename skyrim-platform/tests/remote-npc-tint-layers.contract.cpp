#include "RemoteNpcTintLayers.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <unordered_map>
#include <vector>

struct Head { int hair = 0; int texture = 0; };
struct Layer { Hdn::RemoteFaceLayer data; };
struct Array : std::vector<Layer*> {};
struct Npc { Head* headRelatedData = nullptr; Array* tintLayers = nullptr; };

struct Heap {
  std::unordered_map<void*, int> own;
  int allocations = 0, failAt = -1;
  template <class T> T* create() {
    if (++allocations == failAt) return nullptr;
    auto* memory = std::malloc(sizeof(T));
    assert(memory);
    auto* value = ::new (memory) T();
    own[value] = 1;
    return value;
  }
  template <class T> void destroy(T* value) {
    assert(value && own.erase(value) == 1);
    value->~T(); std::free(value);
  }
  bool install(Npc& npc, const std::vector<Hdn::RemoteFaceLayer>& recipes) {
    return Hdn::InstallImmutableRemoteLayers<Npc, Array, Layer>(&npc, recipes,
      [this]() { return create<Array>(); },
      [this](const Hdn::RemoteFaceLayer& recipe) { auto* value = create<Layer>(); if (value) value->data = recipe; return value; },
      [this](Array* value) { destroy(value); }, [this](Layer* value) { destroy(value); });
  }
  void engineDestroy(Npc& npc) {
    if (npc.tintLayers) {
      for (auto* layer : *npc.tintLayers) destroy(layer);
      destroy(npc.tintLayers); npc.tintLayers = nullptr;
    }
    if (npc.headRelatedData) { destroy(npc.headRelatedData); npc.headRelatedData = nullptr; }
  }
};

int main() {
  int cases = 0;
  const std::vector<Hdn::RemoteFaceMask> masks = {
    {6, 0xffa08060u, "Data/Textures/Actors/character/TintMasks/Skin.dds"},
    {7, 0x80203040u, "ACTORS\\character\\TintMasks\\Paint.dds"}
  };
  const std::vector<Hdn::RemoteFaceAsset> assets = {
    {6, 42, "actors\\CHARACTER\\tintmasks\\skin.dds", {{3, 0xa08060}}},
    {7, 81, "textures/actors/character/tintmasks/paint.dds", {}}
  };
  const auto plan = Hdn::PrepareRemoteFaceLayers(masks, assets);
  assert(plan.valid && plan.layers.size() == 2 && plan.matchedCount == 2 && plan.missingCount == 0);
  assert(plan.layers[0].tintIndex == 42 && plan.layers[0].preset == 3);
  assert(plan.layers[0].argb == masks[0].argb && plan.layers[0].interpolationValue == 100);
  assert(plan.layers[1].tintIndex == 81 && plan.layers[1].interpolationValue == 50);
  cases++;
  {
    auto wrongType = masks; wrongType[0].type = 5;
    auto result = Hdn::PrepareRemoteFaceLayers(wrongType, assets);
    assert(result.valid && result.layers.size() == 1 && result.missingCount == 1);
    cases++;
  }
  {
    auto wrongSkin = masks; wrongSkin[0].texturePath = "elsewhere.dds";
    auto result = Hdn::PrepareRemoteFaceLayers(wrongSkin, assets);
    assert(!result.valid && result.reason == "skin-tone-not-in-race"); cases++;
  }
  for (const std::string path : {"../bad.dds", "Data/Textures/a/../bad.dds", "C:/bad.dds", ""}) {
    auto invalid = masks; invalid[0].texturePath = path;
    assert(!Hdn::PrepareRemoteFaceLayers(invalid, assets).valid); cases++;
  }
  {
    auto duplicate = masks; duplicate.push_back(masks.front());
    assert(!Hdn::PrepareRemoteFaceLayers(duplicate, assets).valid); cases++;
    auto ambiguous = assets; ambiguous.push_back(assets.front()); ambiguous.back().tintIndex = 999;
    assert(!Hdn::PrepareRemoteFaceLayers(masks, ambiguous).valid); cases++;
  }
  for (int alpha : {0, 1, 127, 128, 254, 255}) {
    auto values = masks; values[0].argb = (std::uint32_t(alpha) << 24) | 0xa08060;
    auto result = Hdn::PrepareRemoteFaceLayers(values, assets);
    assert(result.valid && result.layers[0].interpolationValue <= 100);
    assert(result.layers[0].interpolationValue == (alpha * 100 + 127) / 255); cases++;
  }
  {
    auto many = masks; many.resize(129, masks.front());
    assert(!Hdn::PrepareRemoteFaceLayers(many, assets).valid); cases++;
  }
  for (int fail = 1; fail <= 3; fail++) {
    Heap heap; heap.failAt = fail; Npc npc;
    assert(!heap.install(npc, plan.layers));
    assert(npc.tintLayers == nullptr && heap.own.empty()); cases++;
  }
  {
    Heap heap; Npc npc;
    assert(heap.install(npc, plan.layers));
    auto* outer = npc.tintLayers; auto* first = outer->at(0);
    assert(heap.own.size() == 3);
    assert(!heap.install(npc, plan.layers)); // published state is immutable
    assert(npc.tintLayers == outer && npc.tintLayers->at(0) == first);
    assert(first->data.argb == 0xffa08060u);
    // Unpublished ownership is gone; only the engine tears it down, once.
    heap.engineDestroy(npc); assert(heap.own.empty()); cases++;
  }
  {
    Heap heap; Head borrowed{11, 22}; Array borrowedLayers;
    Npc clone1{&borrowed, &borrowedLayers}, clone2{&borrowed, &borrowedLayers};
    assert(Hdn::IsolateRemoteAppearanceData(&clone1, [&]() { return heap.create<Head>(); }));
    assert(Hdn::IsolateRemoteAppearanceData(&clone2, [&]() { return heap.create<Head>(); }));
    assert(clone1.tintLayers == nullptr && clone2.tintLayers == nullptr);
    clone1.headRelatedData->hair = 99; clone1.headRelatedData->texture = 88;
    assert(borrowed.hair == 11 && borrowed.texture == 22);
    assert(clone2.headRelatedData->hair == 11 && clone2.headRelatedData->texture == 22);
    heap.engineDestroy(clone1); heap.engineDestroy(clone2);
    assert(heap.own.empty()); cases++;
  }
  {
    Heap heap; heap.failAt = 1; Head borrowed{11,22}; Array borrowedLayers;
    Npc failed{&borrowed, &borrowedLayers};
    assert(!Hdn::IsolateRemoteAppearanceData(&failed, [&]() { return heap.create<Head>(); }));
    assert(failed.headRelatedData == nullptr && failed.tintLayers == nullptr);
    assert(borrowed.hair == 11 && borrowed.texture == 22 && heap.own.empty()); cases++;
  }
  for (int process = 0; process < 100; process++) {
    Heap heap; Npc npc;
    assert(heap.install(npc, plan.layers));
    assert(npc.tintLayers->at(0)->data.tintIndex == 42);
    heap.engineDestroy(npc); assert(heap.own.empty());
  }
  cases++;
  std::cout << "PASS " << cases << " CPU layer and lifetime contracts; no Windows renderer executed\n";
}
