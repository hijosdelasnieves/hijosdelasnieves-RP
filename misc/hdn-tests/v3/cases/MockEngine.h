#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Deliberately models only identity, readiness and the engine entry point.
// No texture factory, renderer buffer or GPU destructor is implemented here.
namespace RE {
struct Actor;
struct TESNPC;
struct BSFaceGenNiNode;
struct BGSHeadPart;
struct ActorHandle;
}
namespace Test {
inline bool ae = true;
inline unsigned handleLookups = 0, engineCalls = 0, engineCallsThisUpdate = 0;
inline std::vector<std::uint32_t> regenerated;
inline std::function<void(RE::BSFaceGenNiNode*, RE::BGSHeadPart*, RE::TESNPC*)> onEngine;
inline std::deque<std::function<void()>> pendingTasks;
inline bool tasksAvailable = true, throwOnEnqueue = false;
inline std::vector<std::string> diagnosticLines;
}
namespace logger {
template <class... Values> void info(const char*, Values...) {}
template <class... Values> void warn(const char*, Values...) {}
}
namespace RE {
struct StaticFunctionTag {};
struct TintMask { std::uint32_t token = 0; };
template <class T> using BSTArray = std::vector<T>;
struct BGSHeadPart {
  enum class HeadPartType { kFace = 1 };
  using Type = HeadPartType;
};
struct BSShaderMaterial {
  enum class Feature { kFaceGen = 4, kFaceGenRGBTint = 5 };
  virtual ~BSShaderMaterial() = default;
  virtual Feature GetFeature() const { return feature; }
  Feature feature = Feature::kFaceGen;
};
struct BSLightingShaderMaterialFacegen : BSShaderMaterial {
  bool tintTexture = false;
};
struct NiProperty { virtual ~NiProperty() = default; };
struct BSLightingShaderProperty : NiProperty { BSShaderMaterial* material = nullptr; };
struct BSGeometry;
struct NiAVObject {
  virtual ~NiAVObject() = default;
  virtual BSGeometry* AsGeometry() { return nullptr; }
};
struct BSGeometry : NiAVObject {
  struct States { enum { kProperty, kEffect, kTotal }; };
  struct PropertyRef {
    NiProperty* value = nullptr;
    NiProperty* get() const { return value; }
  };
  struct Runtime { std::array<PropertyRef, 2> properties; } runtime;
  BSGeometry* AsGeometry() override { return this; }
  Runtime& GetGeometryRuntimeData() { return runtime; }
};
struct BSFaceGenNiNode {
  unsigned references = 0;
  void IncRefCount() { ++references; }
  void DecRefCount() { assert(references); --references; }
};
template <class T> class NiPointer {
public:
  NiPointer() = default;
  explicit NiPointer(T* value) : value_(value, [](T*) {}) {}
  NiPointer(const std::shared_ptr<T>& value) : value_(value) {}
  NiPointer& operator=(const std::shared_ptr<T>& value) { value_ = value; return *this; }
  T* get() const { return value_.get(); }
  T* operator->() const { return value_.get(); }
  explicit operator bool() const { return value_ != nullptr; }
private:
  std::shared_ptr<T> value_;
};
struct TESNPC {
  using HeadPartType = BGSHeadPart::HeadPartType;
  std::uint32_t formID = 0xff000900;
  BGSHeadPart facePart;
  bool hasFacePart = true;
  std::uint32_t GetFormID() const { return formID; }
  BGSHeadPart* GetCurrentHeadPartByType(HeadPartType type) {
    assert(type == HeadPartType::kFace);
    return hasFacePart ? &facePart : nullptr;
  }
};
struct TESObjectREFR {
  virtual ~TESObjectREFR() = default;
  template <class T> T* As() { return dynamic_cast<T*>(this); }
};
struct Actor : TESObjectREFR, std::enable_shared_from_this<Actor> {
  std::uint32_t formID;
  TESNPC base;
  BSFaceGenNiNode face;
  bool hasBase = true, loaded = true, hasFace = true;
  bool deleted = false, marked = false;
  NiAVObject* head = nullptr;
  Actor(std::uint32_t id = 0xff000100, std::uint32_t baseId = 0xff000900)
    : formID(id) { base.formID = baseId; }
  std::uint32_t GetFormID() const { return formID; }
  TESNPC* GetActorBase() { return hasBase ? &base : nullptr; }
  bool IsDeleted() const { return deleted; }
  bool IsMarkedForDeletion() const { return marked; }
  bool Is3DLoaded() const { return loaded; }
  BSFaceGenNiNode* GetFaceNodeSkinned() { return hasFace ? &face : nullptr; }
  BSFaceGenNiNode* GetFaceNode() { return hasFace ? &face : nullptr; }
  NiAVObject* GetHeadPartObject(BGSHeadPart::HeadPartType) { return head; }
  ActorHandle GetHandle();
};
struct ActorHandle {
  std::weak_ptr<Actor> generation;
  ActorHandle() = default;
  explicit ActorHandle(Actor* actor) : generation(actor->weak_from_this()) {}
  std::shared_ptr<Actor> get() const { ++Test::handleLookups; return generation.lock(); }
  bool operator==(const ActorHandle& other) const {
    return !generation.owner_before(other.generation) && !other.generation.owner_before(generation);
  }
};
inline ActorHandle Actor::GetHandle() { return ActorHandle(this); }
struct PlayerCharacter {
  BSTArray<TintMask*> local;
  static PlayerCharacter* GetSingleton() { static PlayerCharacter self; return &self; }
  BSTArray<TintMask*>* GetTintList() { return &local; }
};
struct BSFaceGenManager {
  static inline BSFaceGenManager* singleton = nullptr;
  static BSFaceGenManager* GetSingleton() { return singleton; }
  void PrepareHeadPartForShaders(BSFaceGenNiNode* node, BGSHeadPart* part, TESNPC* npc) {
    assert(node && part && npc);
    ++Test::engineCalls; ++Test::engineCallsThisUpdate;
    if (Test::onEngine) Test::onEngine(node, part, npc);
  }
};
enum class BSEventNotifyControl { kContinue };
struct TESFormDeleteEvent { std::uint32_t formID; };
}
namespace REL {
struct Version {
  std::array<unsigned, 4> value;
  constexpr Version(unsigned a, unsigned b, unsigned c, unsigned d = 0) : value{a,b,c,d} {}
  bool operator==(const Version&) const = default;
};
struct Module {
  inline static Version runtimeVersion{1, 6, 1170, 0};
  static bool IsAE() { return Test::ae; }
  static Version getRuntime() { return runtimeVersion; }
  static Version GetRuntime() { return getRuntime(); }
  static Module& get() { static Module module; return module; }
  Version version() const { return runtimeVersion; }
};
}

template <class To, class From>
To netimmerse_cast(From* from) { return dynamic_cast<To>(from); }

namespace SKSE {
struct TaskInterface {
  void AddTask(std::function<void()> task) {
    if (Test::throwOnEnqueue) throw std::bad_alloc();
    Test::pendingTasks.push_back(std::move(task));
    assert(Test::pendingTasks.size() <= 1);
  }
};
inline TaskInterface* GetTaskInterface() {
  static TaskInterface tasks;
  return Test::tasksAvailable ? &tasks : nullptr;
}
}
