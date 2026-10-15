#include "RemoteFaceRegenQueue.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>

using Queue = Hdn::RemoteFaceRegenQueue;
using Result = Queue::Result;
constexpr std::uint32_t actor0 = 0xff000100, base0 = 0xff000900;

Queue::Ticket Ready(Queue& queue)
{
  assert(!queue.Tick()); assert(!queue.Tick());
  auto ticket = queue.Tick(); assert(ticket && queue.IsCurrent(*ticket));
  return *ticket;
}

int main()
{
  static_assert(Queue::kMaximumCapacity == 256);
  static_assert(Queue::kMaximumAttempts == 4);
  static_assert(Queue::kDelayUpdates == 2);
  static_assert(Queue::kLifetimeUpdates == 300);

  Queue q;
  assert(q.Request(actor0, base0));
  auto ticket = Ready(q);
  assert(ticket.actor == actor0 && ticket.base == base0 && q.InFlight());
  for (unsigned i = 0; i < 10; ++i) assert(!q.Tick());
  q.Complete(ticket, Result::Done);
  assert(q.Empty() && !q.InFlight());
  for (unsigned i = 0; i < 1000; ++i) assert(!q.Tick());
  std::cout << "PASS exact two-update delay; one physical callback in flight; zero idle work\n";

  q = Queue{};
  for (unsigned i = 0; i < 256; ++i) assert(q.Request(actor0 + i, base0 + i));
  assert(!q.Request(actor0 + 256, base0 + 256));
  std::set<std::uint32_t> regenerated;
  unsigned ticks = 0;
  while (!q.Empty() && ticks < 300) {
    if (auto next = q.Tick()) {
      assert(regenerated.insert(next->actor).second);
      q.Complete(*next, Result::Done);
    }
    ++ticks;
  }
  assert(q.Empty() && regenerated.size() == 256 && ticks < 300);
  std::cout << "PASS 256 actors finish before TTL with <=1 scheduled callback/update (" << ticks << " updates)\n";

  q = Queue{};
  assert(q.Request(actor0, base0));
  for (unsigned attempt = 1; attempt <= 4; ++attempt) {
    ticket = Ready(q);
    q.Complete(ticket, Result::Retry);
    assert(q.Empty() == (attempt == 4));
  }
  assert(!q.Tick());
  std::cout << "PASS four total attempts per revision and two-update retry backoff\n";

  q = Queue{};
  for (unsigned i = 0; i < 256; ++i) assert(q.Request(actor0 + i, base0 + i));
  std::set<std::uint32_t> attempted;
  for (unsigned i = 0; i < 258; ++i) {
    if (auto next = q.Tick()) {
      attempted.insert(next->actor);
      q.Complete(*next, Result::Retry);
    }
  }
  assert(attempted.size() == 256);
  std::cout << "PASS round-robin fairness: all 256 owners receive a turn\n";

  q = Queue{};
  for (unsigned i = 0; i < 256; ++i) assert(q.Request(actor0 + i, base0 + i));
  ticket = Ready(q);
  for (unsigned clock = 4; clock <= 299; ++clock) assert(!q.Tick());
  assert(q.Size() == 256 && q.IsCurrent(ticket));
  assert(!q.Tick());
  assert(q.Size() == 0 && !q.IsCurrent(ticket) && q.InFlight() && !q.Empty());
  q.Complete(ticket, Result::Retry);
  assert(q.Empty());
  std::cout << "PASS exact TTL expires all entries even when SKSE execution stalls\n";

  q = Queue{};
  assert(q.Request(actor0, base0));
  ticket = Ready(q);
  assert(q.Request(actor0, base0 + 1));
  assert(!q.IsCurrent(ticket));
  for (unsigned i = 0; i < 5; ++i) assert(!q.Tick());
  q.Complete(ticket, Result::Done);
  auto replacement = q.Tick();
  assert(replacement && replacement->base == base0 + 1 && replacement->revision != ticket.revision);
  q.Complete(ticket, Result::Drop);
  assert(q.InFlight() && q.IsCurrent(*replacement));
  q.Complete(*replacement, Result::Done); assert(q.Empty());
  std::cout << "PASS superseded callback keeps physical reservation and cannot retire its replacement\n";

  for (bool load : {false, true}) {
    q = Queue{};
    assert(q.Request(actor0, base0)); ticket = Ready(q);
    if (load) q.Clear(); else q.Cancel(actor0);
    assert(!q.IsCurrent(ticket) && q.Size() == 0 && q.InFlight());
    assert(q.Request(actor0, base0));
    for (unsigned i = 0; i < 5; ++i) assert(!q.Tick());
    q.Complete(ticket, Result::Drop);
    replacement = q.Tick();
    assert(replacement && replacement->revision != ticket.revision);
    q.Complete(*replacement, Result::Done); assert(q.Empty());
  }
  std::cout << "PASS cancel/load invalidate ticket while preserving in-flight reservation until completion\n";

  q = Queue{};
  assert(q.Request(actor0, base0)); q.Tick(); q.Tick();
  assert(q.Request(actor0, base0 + 1));
  ticket = Ready(q); assert(ticket.base == base0 + 1);
  q.Complete(ticket, Result::Drop); assert(q.Empty());
  assert(!q.Request(0x14, base0)); assert(!q.Request(0xffffffff, base0));
  assert(q.Request(actor0, base0)); assert(!q.Request(actor0, 0x7)); assert(q.Empty());
  std::cout << "PASS coalescing resets delay and invalid actor/base cancels pending work\n";
}
