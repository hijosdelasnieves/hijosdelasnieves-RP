#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <vector>

namespace Test {
inline bool ae = true, factoryFails = false, resourceFails = false;
inline bool factoryHasResource = false;
inline int factoryCalls = 0, renderCalls = 0, liveTextures = 0, liveResources = 0;
inline std::function<void()> onRender;
}
namespace logger {
template <class... T> void warn(const char*, T...) {}
}

namespace RE {
class NiRefObject {
public:
  virtual ~NiRefObject() = default;
  void IncRefCount() { ++refs; }
  void DecRefCount() { assert(refs > 0); if (!--refs && allocated) delete this; }
  std::uint32_t refs = 0;
  bool allocated = false;
  std::uint8_t padding[3]{};
};
template <class T> class NiPointer {
  T* ptr = nullptr;
public:
  NiPointer() = default;
  explicit NiPointer(T* p) { reset(p); }
  NiPointer(const NiPointer& rhs) { reset(rhs.ptr); }
  NiPointer& operator=(const NiPointer& rhs) { reset(rhs.ptr); return *this; }
  ~NiPointer() { reset(); }
  void reset(T* p = nullptr) { if (ptr == p) return; auto old = ptr; ptr = p; if (ptr) ptr->IncRefCount(); if (old) old->DecRefCount(); }
  T* get() const { return ptr; }
  T* operator->() const { return ptr; }
  explicit operator bool() const { return ptr != nullptr; }
};
template <class T> using BSTArray = std::vector<T>;
class TintMask {};
class BSFixedString { public: explicit BSFixedString(const char*) {} };
namespace BSGraphics { class Texture; }
class NiTexture : public NiRefObject {
public:
  char padding[0x30]{};
  struct RendererData {
    RendererData() { ++Test::liveResources; }
    ~RendererData() { --Test::liveResources; }
  };
};
class NiSourceTexture : public NiTexture {
public:
  void* stream = nullptr;
  BSGraphics::Texture* rendererTexture = nullptr;
  NiSourceTexture() { allocated = true; ++Test::liveTextures; }
  ~NiSourceTexture() override { delete reinterpret_cast<RendererData*>(rendererTexture); --Test::liveTextures; }
};
class BSShaderMaterial {
public:
  enum class Feature { kFaceGen = 4, kFaceGenRGBTint = 5 };
  virtual ~BSShaderMaterial() = default;
  virtual Feature GetFeature() const { return feature; }
  unsigned IncRef() { return ++refs; }
  unsigned DecRef() { assert(refs); return --refs; }
  unsigned refs = 0;
  Feature feature = Feature::kFaceGen;
  char reserved[0x90]{};
};
template <class T> class BSTSmartPointer {
  T* p;
public:
  explicit BSTSmartPointer(T* value): p(value) { p->IncRef(); }
  ~BSTSmartPointer() { p->DecRef(); }
};
class BSLightingShaderMaterialFacegen : public BSShaderMaterial {
public:
  NiPointer<NiSourceTexture> tintTexture;
};
class NiProperty : public NiRefObject {};
class BSLightingShaderProperty : public NiProperty {
public: BSShaderMaterial* material = nullptr;
};
class BSGeometry;
class NiAVObject : public NiRefObject {
public: virtual BSGeometry* AsGeometry() { return nullptr; }
};
class BSGeometry : public NiAVObject {
public:
  struct States { enum { kProperty, kEffect, kTotal }; };
  struct Runtime { NiPointer<NiProperty> properties[2]; } runtime;
  BSGeometry* AsGeometry() override { return this; }
  Runtime& GetGeometryRuntimeData() { return runtime; }
};
class BGSHeadPart { public: enum class HeadPartType { kFace = 1 }; };
class TESNPC { public: std::uint32_t id = 0xff000100; std::uint32_t GetFormID() const { return id; } };
class Actor {
public:
  std::uint32_t id = 0xff000200;
  bool deleted = false, marked = false, loaded = true;
  TESNPC* base = nullptr;
  NiAVObject* head = nullptr;
  std::uint32_t GetFormID() const { return id; }
  TESNPC* GetActorBase() const { return base; }
  bool IsDeleted() const { return deleted; }
  bool IsMarkedForDeletion() const { return marked; }
  bool Is3DLoaded() const { return loaded; }
  NiAVObject* GetHeadPartObject(BGSHeadPart::HeadPartType) { return head; }
};
namespace BSGraphics {
class Renderer {
public:
  static inline Renderer* singleton = nullptr;
  static Renderer* GetSingleton() { return singleton; }
  NiTexture::RendererData* CreateRenderTexture(std::uint32_t width, std::uint32_t height) {
    assert(width == 512 && height == 512);
    return Test::resourceFails ? nullptr : new NiTexture::RendererData;
  }
};
}
}

template <class To, class From> To netimmerse_cast(From* from) { return dynamic_cast<To>(from); }

namespace REL {
struct ID { explicit ID(int n) { assert(n == 70717); } };
struct Module { static bool IsAE() { return Test::ae; } };
template <class Function> class Relocation {
public:
  explicit Relocation(ID) {}
  RE::NiSourceTexture* operator()(RE::BSFixedString&) {
    ++Test::factoryCalls;
    if (Test::factoryFails) return nullptr;
    auto p = new RE::NiSourceTexture;
    if (Test::factoryHasResource) p->rendererTexture = reinterpret_cast<RE::BSGraphics::Texture*>(new RE::NiTexture::RendererData);
    return p;
  }
};
}
