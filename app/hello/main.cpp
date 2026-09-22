#include <print>

// Step 0.1: prove the toolchain works in both build configs.
// We print the active build type (baked in by CMake) so that running the
// binary visibly confirms *which* configuration produced it.

#ifndef HFT_BUILD_CONFIG
#define HFT_BUILD_CONFIG "unknown"
#endif

int main() {
  // std::print is a C++23 library feature; if this links, the standard
  // library half of the toolchain is good too, not just the compiler.
  std::println("Hello from the hft engine [{} build]", HFT_BUILD_CONFIG);
  return 0;
}
