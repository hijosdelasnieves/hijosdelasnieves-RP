#include "Policy.hpp"
#include <cassert>
#include <limits>
int main()
{
  using namespace hdn::gameplay;
  assert(supportedRuntime(1, 6, 1170, 0));
  assert(!supportedRuntime(1, 5, 97, 0));
  assert(!supportedRuntime(1, 6, 640, 0));
  assert(!supportedRuntime(1, 7, 99, 0));
  assert(cellOf(18147.296875f) == 4);
  assert(cellOf(-17635.494140625f) == -5);
  assert(cellOf(-0.1f) == -1);
  assert(cellOf(-4096) == -1);
  assert(validDestination(18147, -17635, -4479));
  assert(!validDestination(std::numeric_limits<float>::infinity(), 0, 0));
  assert(!validDestination(1e8f, 0, 0));
  assert(atDestination(0x3c, 18147, -17635, 0x3c, 18147, -17635));
  assert(!atDestination(0x3c, 0, 0, 0x3c, 18147, -17635));
  assert(!atDestination(0x3c, 18147, -17635, 0x1a, 18147, -17635));
  assert(!atDestination(0, 0, 0, 0, 0, 0));
  InputGate g;
  g.open();
  assert(g.mode == InputMode::Chat);
  assert(!g.neutral(true)); // typing a space is never a resume
  g.close();
  assert(g.mode == InputMode::AwaitNeutral);
  assert(!g.neutral(false)); // W held across ESC
  g.close();                 // duplicate blur/send is harmless
  assert(!g.neutral(false)); // no timeout rearms a held key
  assert(g.neutral(true));   // release all, then require fresh input
  assert(g.mode == InputMode::Idle);
  assert(!g.neutral(true));
  g.open();
  g.close();
  g.open();
  assert(!g.neutral(true)); // stale close cannot release reopened chat
}
