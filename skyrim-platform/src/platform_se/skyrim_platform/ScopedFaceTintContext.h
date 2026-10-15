#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Hdn {

// The entire synchronous engine call owns this context, including repeated
// ApplyMasks calls. Nested calls restore their parent; other threads have none.
template <class Snapshot>
class ScopedFaceTintContext
{
public:
  struct State
  {
    std::uint32_t actorId;
    std::uint32_t baseId;
    std::shared_ptr<const Snapshot> snapshot;
    std::size_t applyCount = 0;
    std::int32_t shader = -1;
    bool texture = false;
  };

  ScopedFaceTintContext(std::uint32_t actor, std::uint32_t base,
                        std::shared_ptr<const Snapshot> snapshot,
                        std::int32_t shader, bool texture)
    : state_{ actor, base, std::move(snapshot), 0, shader, texture }
    , previous_(current_)
  {
    current_ = &state_;
  }
  ~ScopedFaceTintContext() { current_ = previous_; }
  ScopedFaceTintContext(const ScopedFaceTintContext&) = delete;
  ScopedFaceTintContext& operator=(const ScopedFaceTintContext&) = delete;

  [[nodiscard]] static State* Current() noexcept { return current_; }
  [[nodiscard]] std::size_t ApplyCount() const noexcept { return state_.applyCount; }

private:
  State state_;
  State* previous_;
  inline static thread_local State* current_ = nullptr;
};
}
