#pragma once

// Minimal assertion harness shared by the host tests (no framework needed).

#include <cmath>
#include <cstdio>

namespace testing_lite {

inline int gFailures = 0;
inline int gChecks = 0;

inline void check(bool condition, const char *expr, const char *file, int line) {
  ++gChecks;
  if (!condition) {
    ++gFailures;
    std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expr);
  }
}

} // namespace testing_lite

#define CHECK(expr)                                                            \
  ::testing_lite::check((expr), #expr, __FILE__, __LINE__)
#define CHECK_NEAR(a, b)                                                       \
  ::testing_lite::check(std::fabs((a) - (b)) < 1e-6, #a " ~= " #b, __FILE__,   \
                        __LINE__)
