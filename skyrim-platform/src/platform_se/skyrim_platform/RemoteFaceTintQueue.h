#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Hdn {

// Game-independent scheduling policy. Stores integer identity only; callers
// resolve and validate native handles inside the callback, then return Drop
// for a missing owner, a changed base or an unsupported rendering feature.
// The owner serializes Request/Cancel/Tick on its game thread (or externally).
//
// Call Tick once per game update while !Empty(). A request skips the next two
// ticks and first becomes eligible on the third. Its lifetime ends on tick
// 120 after the latest request, regardless of the work budget or retry count.
class RemoteFaceTintQueue
{
public:
  static constexpr std::size_t kMaximumCapacity = 256;
  static constexpr std::size_t kWorkBudget = 4;
  static constexpr std::uint64_t kDelayUpdates = 2;
  static constexpr std::uint64_t kLifetimeUpdates = 120;

  enum class Result
  {
    Retry,
    Done,
    Drop
  };

  struct TickStats
  {
    std::size_t attempted = 0;
    std::size_t done = 0;
    std::size_t dropped = 0;
    std::size_t expired = 0;
    std::size_t superseded = 0;
  };

  explicit RemoteFaceTintQueue(
    std::size_t capacity = kMaximumCapacity) noexcept
    : capacity_(capacity > kMaximumCapacity ? kMaximumCapacity : capacity)
  {
  }

  // One actor can have only one pending expected base. Repeated requests
  // coalesce in place, without losing their round-robin position. A changed
  // expected base supersedes the previous identity. On overflow keep existing
  // owners; the caller can observe false rather than silently evicting one.
  [[nodiscard]] bool Request(std::uint32_t actor,
                             std::uint32_t expectedBase) noexcept
  {
    if (!IsDynamic(actor)) {
      return false;
    }
    if (!IsDynamic(expectedBase)) {
      Cancel(actor);
      return false;
    }

    for (std::size_t i = 0; i < capacity_; ++i) {
      auto& entry = entries_[i];
      if (entry.active && entry.actor == actor) {
        entry.base = expectedBase;
        entry.requestedAt = clock_;
        entry.revision = NextRevision();
        return true;
      }
    }

    if (size_ == capacity_) {
      return false;
    }
    for (std::size_t checked = 0; checked < capacity_; ++checked) {
      const auto index = insertionCursor_;
      insertionCursor_ = (insertionCursor_ + 1) % capacity_;
      auto& entry = entries_[index];
      if (!entry.active) {
        entry = { actor, expectedBase, clock_, NextRevision(), true };
        ++size_;
        return true;
      }
    }
    return false;
  }

  // ClearTintMasks(actor) calls this before retiring the old tint snapshot.
  // It is also safe to cancel or replace an actor inside a Tick callback.
  void Cancel(std::uint32_t actor) noexcept
  {
    for (std::size_t i = 0; i < capacity_; ++i) {
      auto& entry = entries_[i];
      if (entry.active && entry.actor == actor) {
        Retire(entry);
        return;
      }
    }
  }

  [[nodiscard]] bool Empty() const noexcept { return size_ == 0; }
  [[nodiscard]] std::size_t Size() const noexcept { return size_; }
  [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }

  template <class Callback>
  TickStats Tick(Callback&& callback)
  {
    TickStats stats;
    if (Empty() || ticking_) {
      return stats;
    }
    ticking_ = true;
    ++clock_;

    // Check expiry for every live slot before applying the work budget. A
    // delayed/not-ready actor cannot live forever by losing its turn.
    for (std::size_t i = 0; i < capacity_; ++i) {
      auto& entry = entries_[i];
      if (entry.active && clock_ - entry.requestedAt >= kLifetimeUpdates) {
        Retire(entry);
        ++stats.expired;
      }
    }

    for (std::size_t checked = 0;
         checked < capacity_ && stats.attempted < kWorkBudget;
         ++checked) {
      const auto index = workCursor_;
      workCursor_ = (workCursor_ + 1) % capacity_;
      auto& entry = entries_[index];
      if (!entry.active || clock_ - entry.requestedAt <= kDelayUpdates) {
        continue;
      }

      const auto actor = entry.actor;
      const auto base = entry.base;
      const auto revision = entry.revision;
      ++stats.attempted;
      Result result = Result::Drop;
      try {
        result = callback(actor, base);
      } catch (...) {
        // A failed native lookup/render attempt must not escape the game
        // update or become a permanent retry. A newer request still wins.
        result = Result::Drop;
      }

      if (!entry.active || entry.actor != actor || entry.base != base ||
          entry.revision != revision) {
        ++stats.superseded;
        continue;
      }

      if (result == Result::Retry) {
        continue;
      }
      Retire(entry);
      if (result == Result::Done) {
        ++stats.done;
      } else {
        ++stats.dropped;
      }
    }

    ticking_ = false;
    return stats;
  }

private:
  struct Entry
  {
    std::uint32_t actor = 0;
    std::uint32_t base = 0;
    std::uint64_t requestedAt = 0;
    std::uint64_t revision = 0;
    bool active = false;
  };

  static bool IsDynamic(std::uint32_t id) noexcept
  {
    return id >= 0xff000000 && id != 0xffffffff;
  }

  std::uint64_t NextRevision() noexcept
  {
    if (++revision_ == 0) {
      ++revision_;
    }
    return revision_;
  }

  void Retire(Entry& entry) noexcept
  {
    entry.active = false;
    --size_;
  }

  std::array<Entry, kMaximumCapacity> entries_{};
  std::size_t capacity_;
  std::size_t size_ = 0;
  std::size_t insertionCursor_ = 0;
  std::size_t workCursor_ = 0;
  std::uint64_t clock_ = 0;
  std::uint64_t revision_ = 0;
  bool ticking_ = false;
};

} // namespace Hdn
