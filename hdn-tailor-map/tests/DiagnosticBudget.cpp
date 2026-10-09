#include "HdnDiagnostic.h"
#include <iostream>
#include <thread>
#include <vector>

int main()
{
  std::atomic<unsigned> successes{ 0 };
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < 8; ++i) {
    threads.emplace_back([&]() {
      for (unsigned j = 0; j < 10000; ++j) {
        if (HdnDiagnostic::Take(HdnDiagnostic::importCalls, 128)) {
          ++successes;
        }
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  if (successes != 128 || HdnDiagnostic::importCalls != 128) {
    return 1;
  }
  for (unsigned i = 0; i < 1000; ++i) {
    HdnDiagnostic::Take(HdnDiagnostic::doorCalls, 32);
    HdnDiagnostic::Take(HdnDiagnostic::targetCalls, 64);
  }
  if (HdnDiagnostic::doorCalls != 32 || HdnDiagnostic::targetCalls != 64) {
    return 2;
  }
  HdnDiagnostic::ResetDoors();
  if (HdnDiagnostic::doorCalls != 0 || HdnDiagnostic::targetCalls != 0 ||
      HdnDiagnostic::importCalls != 128) {
    return 3;
  }
  std::cout << "PASS: bounded 8-thread logging budgets and per-map reset\n";
}
