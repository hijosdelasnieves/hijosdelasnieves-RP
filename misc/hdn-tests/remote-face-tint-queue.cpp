#include "RemoteFaceTintQueue.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

using Queue = Hdn::RemoteFaceTintQueue;
using Result = Queue::Result;
constexpr std::uint32_t Actor(unsigned i) { return 0xff001000 + i; }
constexpr std::uint32_t Base(unsigned i) { return 0xff002000 + i; }

static void WaitTwoUpdates(Queue& queue)
{
  const auto forbidden = [](auto, auto) -> Result {
    assert(false && "a rebuild needs two complete updates before rendering");
    return Result::Drop;
  };
  assert(queue.Tick(forbidden).attempted == 0);
  assert(queue.Tick(forbidden).attempted == 0);
}

static void DelayCoalescingAndSuccessfulRetirement()
{
  Queue queue;
  assert(queue.Request(Actor(1), Base(1)));
  WaitTwoUpdates(queue);
  // A second mask/rebuild arrives just before rendering. The stale request
  // must not render its earlier snapshot in this or the next two updates.
  assert(queue.Request(Actor(1), Base(1)));
  assert(queue.Size() == 1);
  WaitTwoUpdates(queue);
  int calls = 0;
  const auto stats = queue.Tick([&](auto actor, auto base) {
    ++calls;
    assert(actor == Actor(1) && base == Base(1));
    return Result::Done;
  });
  assert(calls == 1 && stats.done == 1 && queue.Empty());
  for (int i = 0; i < 200; ++i) {
    assert(queue.Tick([&](auto, auto) { ++calls; return Result::Done; }).attempted == 0);
  }
  assert(calls == 1);
}

static void FairnessWithPermanentNotReadyOwners()
{
  Queue queue(12);
  for (unsigned i = 0; i < 12; ++i) assert(queue.Request(Actor(i), Base(i)));
  WaitTwoUpdates(queue);
  std::array<unsigned, 12> visits{};
  std::vector<unsigned> finished;
  for (int frame = 0; frame < 3; ++frame) {
    const auto stats = queue.Tick([&](auto actor, auto base) {
      const auto i = actor - Actor(0);
      assert(base == Base(i));
      ++visits[i];
      // Four permanently loading owners must not block the next eight.
      if (i < 4) return Result::Retry;
      finished.push_back(i);
      return Result::Done;
    });
    assert(stats.attempted <= 4);
  }
  assert(finished.size() == 8);
  assert(std::all_of(visits.begin(), visits.end(), [](auto n) { return n == 1; }));
  assert(queue.Size() == 4);
}

static void LifetimeDependsOnUpdatesNotAttemptBudget()
{
  Queue queue;
  for (unsigned i = 0; i < 128; ++i) assert(queue.Request(Actor(i), Base(i)));
  std::array<unsigned, 128> calls{};
  std::size_t expired = 0;
  for (unsigned frame = 1; frame <= 120; ++frame) {
    const auto stats = queue.Tick([&](auto actor, auto) {
      ++calls[actor - Actor(0)];
      return Result::Retry;
    });
    assert(stats.attempted <= 4);
    if (frame < 120) assert(stats.expired == 0);
    expired += stats.expired;
  }
  assert(expired == 128 && queue.Empty());
  assert(*std::max_element(calls.begin(), calls.end()) <= 4);
  assert(*std::min_element(calls.begin(), calls.end()) >= 3);
  // A request refreshes its own deadline, never a different owner's.
  assert(queue.Request(Actor(1), Base(1)));
  assert(queue.Request(Actor(2), Base(2)));
  for (unsigned frame = 1; frame <= 120; ++frame) {
    if (frame == 60) assert(queue.Request(Actor(1), Base(1)));
    queue.Tick([](auto, auto) { return Result::Retry; });
  }
  assert(queue.Size() == 1);
  bool onlyRefreshed = false;
  queue.Tick([&](auto actor, auto) { onlyRefreshed = actor == Actor(1); return Result::Done; });
  assert(onlyRefreshed && queue.Empty());
}

static void TwoHundredActorsFinishBeforeExpiry()
{
  Queue queue;
  for (unsigned i = 0; i < 200; ++i) assert(queue.Request(Actor(i), Base(i)));
  std::array<unsigned, 200> completed{};
  unsigned frames = 0;
  while (!queue.Empty()) {
    ++frames;
    const auto stats = queue.Tick([&](auto actor, auto base) {
      const auto index = actor - Actor(0);
      assert(index < completed.size() && base == Base(index));
      ++completed[index];
      return Result::Done;
    });
    assert(stats.attempted <= 4 && stats.expired == 0);
    assert(frames < 120);
  }
  assert(frames == 52); // two full settling updates, then fifty batches
  assert(std::all_of(completed.begin(), completed.end(), [](auto n) { return n == 1; }));
}

static void TwoHundredActorsWithTwoRendersPerUpdate()
{
  Queue queue;
  for (unsigned i = 0; i < 200; ++i) assert(queue.Request(Actor(i), Base(i)));
  std::array<unsigned, 200> completed{};
  unsigned frames = 0;
  while (!queue.Empty()) {
    ++frames;
    unsigned rendered = 0;
    const auto stats = queue.Tick([&](auto actor, auto base) {
      const auto index = actor - Actor(0);
      assert(index < completed.size() && base == Base(index));
      if (rendered >= 2) return Result::Retry;
      ++rendered;
      ++completed[index];
      return Result::Done;
    });
    assert(rendered <= 2 && stats.attempted <= 4 && stats.expired == 0);
    assert(frames < 120);
  }
  assert(frames == 102);
  assert(std::all_of(completed.begin(), completed.end(), [](auto n) { return n == 1; }));
}

static void ClearOwnerInvalidUnsupportedAndBaseReplacement()
{
  Queue queue;
  assert(queue.Request(Actor(1), Base(1)));
  queue.Cancel(Actor(1));
  assert(queue.Empty());
  assert(queue.Request(Actor(1), Base(1)));
  assert(queue.Request(Actor(1), Base(2))); // the reused reference has a new base
  assert(queue.Size() == 1);
  assert(queue.Request(Actor(3), Base(3)));
  assert(queue.Request(Actor(4), Base(4)));
  WaitTwoUpdates(queue);
  std::vector<std::uint32_t> seen;
  const auto stats = queue.Tick([&](auto actor, auto base) {
    seen.push_back(actor);
    if (actor == Actor(1)) assert(base == Base(2));
    // Owner invalid and unsupported feature are both terminal decisions.
    return actor == Actor(1) ? Result::Done : Result::Drop;
  });
  assert(stats.done == 1 && stats.dropped == 2 && seen.size() == 3 && queue.Empty());
  for (int i = 0; i < 10; ++i) assert(queue.Tick([](auto, auto) { assert(false); return Result::Retry; }).attempted == 0);
}

static void NewRevisionWinsDuringTheRenderCallback()
{
  Queue queue(1);
  assert(queue.Request(Actor(1), Base(1)));
  WaitTwoUpdates(queue);
  auto stats = queue.Tick([&](auto actor, auto) {
    assert(queue.Request(actor, Base(2)));
    return Result::Done; // old result cannot retire the new masks
  });
  assert(stats.superseded == 1 && queue.Size() == 1);
  WaitTwoUpdates(queue);
  stats = queue.Tick([&](auto actor, auto base) {
    assert(actor == Actor(1) && base == Base(2));
    queue.Cancel(actor); // ClearTintMasks while a render callback is running
    assert(queue.Request(Actor(9), Base(9))); // reuses the very same array slot
    throw std::runtime_error("a failed old renderer must not drop the new owner");
    return Result::Drop;
  });
  assert(stats.superseded == 1 && stats.dropped == 0 && queue.Size() == 1);
  WaitTwoUpdates(queue);
  stats = queue.Tick([&](auto actor, auto base) {
    assert(actor == Actor(9) && base == Base(9));
    // A recursively entered Tick does not advance time or process a job twice.
    assert(queue.Tick([](auto, auto) { assert(false); return Result::Drop; }).attempted == 0);
    return Result::Done;
  });
  assert(stats.done == 1 && queue.Empty());
}

static void CapacityValidationAndThrowingLookup()
{
  Queue queue(2);
  assert(!queue.Request(0x14, Base(1)));
  assert(!queue.Request(Actor(1), 7));
  assert(!queue.Request(0xffffffff, Base(1)));
  assert(!queue.Request(Actor(1), 0xffffffff));
  assert(queue.Empty());
  assert(queue.Request(Actor(1), Base(1)));
  assert(queue.Request(Actor(2), Base(2)));
  assert(!queue.Request(Actor(3), Base(3))); // overflow preserves both live owners
  assert(queue.Request(Actor(1), Base(4))); // coalescing is allowed at capacity
  assert(!queue.Request(Actor(2), 7)); // changed to an ineligible base cancels old work
  assert(queue.Size() == 1);
  assert(queue.Request(Actor(3), Base(3)));
  WaitTwoUpdates(queue);
  const auto stats = queue.Tick([](auto, auto) -> Result { throw std::runtime_error("lookup"); });
  assert(stats.dropped == 2 && queue.Empty());
  Queue disabled(0);
  assert(!disabled.Request(Actor(1), Base(1)));
  assert(disabled.Tick([](auto, auto) { assert(false); return Result::Drop; }).attempted == 0);
  assert(Queue(999).Capacity() == 256);
}

int main()
{
  DelayCoalescingAndSuccessfulRetirement();
  FairnessWithPermanentNotReadyOwners();
  LifetimeDependsOnUpdatesNotAttemptBudget();
  TwoHundredActorsFinishBeforeExpiry();
  TwoHundredActorsWithTwoRendersPerUpdate();
  ClearOwnerInvalidUnsupportedAndBaseReplacement();
  NewRevisionWinsDuringTheRenderCallback();
  CapacityValidationAndThrowingLookup();
  std::cout << "PASS remote face queue: delay/coalescing, fair bounded work, frame TTL, cancel/identity, reentrant revisions, unsupported/OOM-free capacity, idle retirement\n";
}
