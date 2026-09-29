// Minimal test harness for tests/native (no dependencies: these tests run
// anywhere a C++20 compiler does, without the SDK or the game).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace sr_test {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& Cases() {
  static std::vector<Case> cases;
  return cases;
}

inline int& Failures() {
  static int failures = 0;
  return failures;
}

struct Register {
  Register(const char* name, void (*fn)()) { Cases().push_back({name, fn}); }
};

inline int RunAll() {
  for (const Case& c : Cases()) {
    const int before = Failures();
    c.fn();
    std::printf("%s %s\n", Failures() == before ? "PASS" : "FAIL", c.name);
  }
  std::printf("%zu tests, %d failed checks\n", Cases().size(), Failures());
  return Failures() ? 1 : 0;
}

}  // namespace sr_test

#define SR_TEST(name)                                      \
  static void name();                                      \
  static sr_test::Register name##_register(#name, name);   \
  static void name()

#define SR_CHECK(cond)                                                               \
  do {                                                                               \
    if (!(cond)) {                                                                   \
      ++sr_test::Failures();                                                         \
      std::printf("  %s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                                \
  } while (0)

#define SR_CHECK_EQ(a, b)                                                            \
  do {                                                                               \
    const auto sr_a = (a);                                                           \
    const auto sr_b = (b);                                                           \
    if (!(sr_a == sr_b)) {                                                           \
      ++sr_test::Failures();                                                         \
      std::printf("  %s:%d: %s == %s failed (%lld vs %lld)\n", __FILE__, __LINE__,   \
                  #a, #b, (long long)sr_a, (long long)sr_b);                          \
    }                                                                                \
  } while (0)
