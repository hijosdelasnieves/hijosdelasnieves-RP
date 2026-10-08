#include "Policy.hpp"
#include <cstdlib>
#include <iostream>

int checks = 0;
void check(bool condition) {
  ++checks;
  if (!condition) { std::cerr << "Failed check " << checks << '\n'; std::exit(1); }
}
int main() {
  using namespace hdn::studio;
  Policy p;
  check(!p.live(0, 0));
  const auto first = p.begin(100);
  check(first > 0 && p.live(first, 100));
  check(p.status(first, 100) == Status::loading);
  check(!p.select(first, 0, 100));
  check(p.select(first, 1, 100));
  check(p.commit(first, 1, 100, true));
  check(p.status(first, 100) == Status::captured);
  p.rendered(true);
  check(p.status(first, 100) == Status::rendered);
  p.rendered(false);
  check(p.status(first, 100) == Status::busy);
  p.rendered(true);
  check(p.status(first, 100) == Status::rendered);
  check(p.select(first, 2, 101));
  check(!p.select(first, 1, 101));
  check(!p.commit(first, 1, 101, true));
  check(p.commit(first, 2, 101, false));
  check(p.status(first, 101) == Status::invalidModel);
  check(p.frame(first, 102, 750, 99));
  check(p.yaw() == 30 && p.zoom() == 1.35f);
  check(!p.frame(first, 102, std::numeric_limits<float>::quiet_NaN(), 1));
  check(!p.frame(first, 102, 0, std::numeric_limits<float>::infinity()));
  check(!p.expire(3101));
  check(p.expire(3102));
  check(!p.frame(first, 3103, 0, 1));
  const auto second = p.begin(4000);
  check(second > first);
  check(!p.end(first));
  check(!p.commit(first, 2, 4001, true));
  check(p.live(second, 4001));
  check(p.end(second));
  check(!p.end(second));
  for (float aspect : {0.6f, 1.0f, 1.7777778f, 2.4f, 3.55f}) {
    auto rig = fit(90, aspect, 0.27f, 0.50f, 1);
    check(std::isfinite(rig.x) && std::isfinite(rig.y) && rig.y < -90);
    check(rig.z == 0);
    check(fit(90, aspect, 0.27f, 0.5f, 1.35f).y > rig.y);
  }
  check(fit(0, 1, 0.5f, 0.5f, 1).radius == 0);
  check(fit(90, 0, 0.5f, 0.5f, 1).radius == 0);
  std::cout << checks << " policy checks passed (not a Skyrim render test)\n";
}
