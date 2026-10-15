#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Hdn {

// Policy only. The caller serializes access and validates generation-bearing
// actor handles when the official SKSE task actually executes.
class RemoteFaceRegenQueue
{
public:
  static constexpr std::size_t kMaximumCapacity = 256;
  static constexpr std::uint64_t kDelayUpdates = 2;
  static constexpr std::uint64_t kLifetimeUpdates = 300;
  static constexpr unsigned kMaximumAttempts = 4;

  enum class Result { Retry, Done, Drop };
  struct Ticket
  {
    std::uint32_t actor = 0;
    std::uint32_t base = 0;
    std::uint64_t revision = 0;
    bool operator==(const Ticket&) const = default;
  };

  [[nodiscard]] bool Request(std::uint32_t actor, std::uint32_t base) noexcept
  {
    if (!IsDynamic(actor) || !IsDynamic(base)) {
      Cancel(actor);
      return false;
    }
    for (auto& entry : entries_) {
      if (entry.active && entry.ticket.actor == actor) {
        entry = MakeEntry(actor, base);
        return true;
      }
    }
    if (size_ == kMaximumCapacity)
      return false;
    for (std::size_t checked = 0; checked < kMaximumCapacity; ++checked) {
      auto& entry = entries_[insertionCursor_];
      insertionCursor_ = (insertionCursor_ + 1) % kMaximumCapacity;
      if (!entry.active) {
        entry = MakeEntry(actor, base);
        ++size_;
        return true;
      }
    }
    return false;
  }

  void Cancel(std::uint32_t actor) noexcept
  {
    for (auto& entry : entries_)
      if (entry.active && entry.ticket.actor == actor) {
        Retire(entry);
        return;
      }
  }

  void Clear() noexcept
  {
    for (auto& entry : entries_)
      entry.active = false;
    size_ = 0;
    // A cancelled SKSE callback still exists until it executes. Keep its
    // physical in-flight reservation so a load cannot enqueue a second one.
  }

  [[nodiscard]] bool Empty() const noexcept { return !size_ && !inFlight_; }
  [[nodiscard]] std::size_t Size() const noexcept { return size_; }
  [[nodiscard]] bool InFlight() const noexcept { return inFlight_.has_value(); }

  // Called once after gameThrQ each update. At most one official SKSE task
  // exists at any time, including cancelled or superseded callbacks.
  std::optional<Ticket> Tick() noexcept
  {
    if (Empty())
      return std::nullopt;
    ++clock_;
    for (auto& entry : entries_)
      if (entry.active && clock_ - entry.requestedAt >= kLifetimeUpdates)
        Retire(entry);
    if (inFlight_)
      return std::nullopt;
    for (std::size_t checked = 0; checked < kMaximumCapacity; ++checked) {
      auto& entry = entries_[workCursor_];
      workCursor_ = (workCursor_ + 1) % kMaximumCapacity;
      if (!entry.active || clock_ < entry.readyAt)
        continue;
      ++entry.attempts;
      inFlight_ = entry.ticket;
      return inFlight_;
    }
    return std::nullopt;
  }

  [[nodiscard]] bool IsCurrent(const Ticket& ticket) const noexcept
  {
    for (const auto& entry : entries_)
      if (entry.active && entry.ticket == ticket)
        return clock_ - entry.requestedAt < kLifetimeUpdates;
    return false;
  }

  void Complete(const Ticket& ticket, Result result) noexcept
  {
    if (inFlight_ && *inFlight_ == ticket)
      inFlight_.reset();
    for (auto& entry : entries_) {
      if (!entry.active || !(entry.ticket == ticket))
        continue;
      if (result == Result::Retry && entry.attempts < kMaximumAttempts &&
          clock_ - entry.requestedAt < kLifetimeUpdates) {
        entry.readyAt = clock_ + kDelayUpdates + 1;
      } else {
        Retire(entry);
      }
      return;
    }
  }

private:
  struct Entry
  {
    Ticket ticket;
    std::uint64_t requestedAt = 0;
    std::uint64_t readyAt = 0;
    unsigned attempts = 0;
    bool active = false;
  };
  static bool IsDynamic(std::uint32_t id) noexcept
  {
    return id >= 0xff000000 && id != 0xffffffff;
  }
  Entry MakeEntry(std::uint32_t actor, std::uint32_t base) noexcept
  {
    if (++revision_ == 0)
      ++revision_;
    return { { actor, base, revision_ }, clock_,
             clock_ + kDelayUpdates + 1, 0, true };
  }
  void Retire(Entry& entry) noexcept
  {
    entry.active = false;
    --size_;
  }

  std::array<Entry, kMaximumCapacity> entries_{};
  std::optional<Ticket> inFlight_;
  std::size_t size_ = 0;
  std::size_t insertionCursor_ = 0;
  std::size_t workCursor_ = 0;
  std::uint64_t clock_ = 0;
  std::uint64_t revision_ = 0;
};
}
