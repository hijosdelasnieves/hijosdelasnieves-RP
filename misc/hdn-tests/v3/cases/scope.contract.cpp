#include "ScopedFaceTintContext.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

using Snapshot = std::vector<unsigned>;
using Scope = Hdn::ScopedFaceTintContext<Snapshot>;

int main()
{
  static_assert(!std::is_copy_constructible_v<Scope>);
  static_assert(!std::is_move_constructible_v<Scope>);
  auto outerMasks = std::make_shared<Snapshot>(Snapshot{11, 12});
  auto innerMasks = std::make_shared<Snapshot>(Snapshot{21, 22});
  std::weak_ptr<Snapshot> outerLifetime = outerMasks;
  assert(!Scope::Current());
  {
    Scope outer(0xff000101, 0xff000901, outerMasks, 4, false);
    auto* const parent = Scope::Current();
    outerMasks.reset();
    assert(!outerLifetime.expired());
    assert(parent && parent->actorId == 0xff000101 && parent->baseId == 0xff000901);
    assert(parent->shader == 4 && !parent->texture && parent->snapshot->at(0) == 11);
    ++parent->applyCount;
    {
      Scope inner(0xff000102, 0xff000902, innerMasks, 5, true);
      assert(Scope::Current() != parent);
      assert(Scope::Current()->actorId == 0xff000102 && Scope::Current()->snapshot->at(0) == 21);
      Scope::Current()->applyCount += 3;
      assert(inner.ApplyCount() == 3 && outer.ApplyCount() == 1);
    }
    assert(Scope::Current() == parent && outer.ApplyCount() == 1);
    try {
      Scope throwing(0xff000103, 0xff000903, innerMasks, 4, false);
      throw std::runtime_error("engine exception");
    } catch (const std::runtime_error&) {}
    assert(Scope::Current() == parent);
    std::thread worker([&] {
      assert(!Scope::Current());
      {
        Scope otherThread(0xff000104, 0xff000904, innerMasks, 4, true);
        assert(Scope::Current()->actorId == 0xff000104);
        ++Scope::Current()->applyCount;
        assert(otherThread.ApplyCount() == 1);
      }
      assert(!Scope::Current());
    });
    worker.join();
    assert(Scope::Current() == parent && outer.ApplyCount() == 1);
  }
  assert(!Scope::Current() && outerLifetime.expired());
  std::cout << "PASS scope: identity/snapshot retention, nested counts/restoration, exception restoration and thread isolation\n";
}
