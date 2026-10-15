#pragma once
#include "MockEngine.h"
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

class TintMask;
namespace Test {
enum class Allocation { Mask, Texture };
inline std::unordered_map<void*, Allocation> allocations;
inline unsigned maskAllocations = 0, maskFrees = 0;
inline unsigned textureAllocations = 0, textureFrees = 0;
inline unsigned textureConstructorCalls = 0, textureConstructions = 0;
inline unsigned textureInitializations = 0, textureDestructions = 0;
inline bool failMaskAllocation = false, failTextureAllocation = false;
inline bool throwTextureConstructor = false, throwTextureInitialization = false;
inline unsigned liveStrings = 0;
inline std::unordered_map<std::string, std::weak_ptr<const std::string>> strings;
}
namespace RE {
class TESTexture;
template <class T> T* malloc(std::size_t size = sizeof(T)) {
  static_assert(std::is_same_v<T, ::TintMask>);
  if (Test::failMaskAllocation) return nullptr;
  auto* pointer = static_cast<T*>(std::malloc(size));
  assert(pointer && Test::allocations.emplace(pointer, Test::Allocation::Mask).second);
  ++Test::maskAllocations;
  return pointer;
}
template <class T> T* calloc(std::size_t count, std::size_t size) {
  static_assert(std::is_same_v<T, TESTexture>);
  if (Test::failTextureAllocation) return nullptr;
  auto* pointer = static_cast<T*>(std::calloc(count, size));
  assert(pointer && Test::allocations.emplace(pointer, Test::Allocation::Texture).second);
  ++Test::textureAllocations;
  return pointer;
}
inline void free(void* pointer) {
  if (!pointer) return;
  const auto found = Test::allocations.find(pointer);
  assert(found != Test::allocations.end());
  if (found->second == Test::Allocation::Mask) ++Test::maskFrees;
  else ++Test::textureFrees;
  Test::allocations.erase(found);
  std::free(pointer);
}
class BSFixedString {
public:
  BSFixedString() = default;
  explicit BSFixedString(const char* text) {
    auto& cached = Test::strings[text];
    value = cached.lock();
    if (!value) {
      ++Test::liveStrings;
      value = std::shared_ptr<const std::string>(new std::string(text), [](const std::string* string) {
        --Test::liveStrings; delete string;
      });
      cached = value;
    }
  }
  std::weak_ptr<const std::string> Weak() const { return value; }
  const char* c_str() const { return value ? value->c_str() : ""; }
private:
  std::shared_ptr<const std::string> value;
};
class TESTexture {
public:
  TESTexture() { ++Test::textureConstructions; }
  virtual ~TESTexture() { ++Test::textureDestructions; }
  virtual void InitializeDataComponent() {
    ++Test::textureInitializations;
    if (Test::throwTextureInitialization) throw std::runtime_error("initialize");
  }
  static void operator delete(void* memory) { RE::free(memory); }
  BSFixedString textureName;
};
}
namespace REL {
struct ID { unsigned value; explicit ID(unsigned number) : value(number) {} };
template <class Function> struct Relocation {
  explicit Relocation(ID id) { assert(id.value == 14953); }
  RE::TESTexture* operator()(RE::TESTexture* texture) const {
    static_assert(std::is_same_v<Function, RE::TESTexture* (*)(RE::TESTexture*)>);
    ++Test::textureConstructorCalls;
    if (Test::throwTextureConstructor) throw std::runtime_error("constructor");
    return ::new (texture) RE::TESTexture();
  }
};
}

#define COLOR_ALPHA(in) ((in >> 24) & 0xFF)
#define COLOR_RED(in) ((in >> 16) & 0xFF)
#define COLOR_GREEN(in) ((in >> 8) & 0xFF)
#define COLOR_BLUE(in) ((in >> 0) & 0xFF)
