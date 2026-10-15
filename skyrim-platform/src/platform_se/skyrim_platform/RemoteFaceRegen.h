#pragma once

#include "RemoteFaceRegenQueue.h"
#include "ScopedFaceTintContext.h"

#include <RE/B/BSTArray.h>
#include <RE/T/TESNPC.h>
#include <memory>
#include <vector>

namespace RE { class Actor; }

namespace Hdn {
struct RemoteFaceTintSnapshot
{
  RE::BSTArray<RE::TintMask*> masks;
  // Type-erased shared owners release properly constructed TESTexture and
  // their TintMask with the game's allocator when the last snapshot retires.
  std::vector<std::shared_ptr<void>> storage;
};
using RemoteFaceTintScope = ScopedFaceTintContext<RemoteFaceTintSnapshot>;

struct RemoteFaceObservation
{
  std::int32_t shader = -1;
  bool texture = false;
};

struct RemoteFaceRegenResult
{
  RemoteFaceRegenQueue::Result result = RemoteFaceRegenQueue::Result::Drop;
  bool engineCalled = false;
  std::size_t applyCount = 0;
  RemoteFaceObservation before;
  RemoteFaceObservation after;
};

// Called only inside an official SKSE task after resolving the actor handle.
// It never creates, casts, retains or releases a GPU texture.
RemoteFaceRegenResult RegenerateRemoteFace(
  RE::Actor* actor, std::uint32_t expectedBase,
  const std::shared_ptr<const RemoteFaceTintSnapshot>& snapshot);

void LogRemoteFaceApply(const RemoteFaceTintScope::State& state) noexcept;
void LogRemoteFaceRegen(std::uint32_t actor, std::uint32_t base,
                        std::size_t masks,
                        const RemoteFaceRegenResult& result) noexcept;
}
