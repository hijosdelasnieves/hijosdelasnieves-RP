#pragma once

namespace hdn::studio {
// Inventory markers orient individual items for the vanilla inventory menu.
// They must not rotate an assembled actor or its captured skeleton. The flag
// is local to this thread and scope: public MRF item rendering is unchanged.
inline thread_local bool actorAssembly = false;
class ActorAssemblyScope
{
public:
  ActorAssemblyScope()
    : previous(actorAssembly)
  {
    actorAssembly = true;
  }
  ~ActorAssemblyScope() { actorAssembly = previous; }
  ActorAssemblyScope(const ActorAssemblyScope&) = delete;
  ActorAssemblyScope& operator=(const ActorAssemblyScope&) = delete;

private:
  bool previous;
};
}
