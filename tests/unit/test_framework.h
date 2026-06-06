// Minimal dependency-free unit-test harness for the offline decoder tests.
//
// Usage:
//   #include "test_framework.h"
//   TEST(name) { CHECK(expr); CHECK_EQ(a, b); }
//   ... more TEST blocks ...
//   TEST_MAIN()
//
// Tests self-register at static-init time; TEST_MAIN() runs them all and
// returns non-zero if any CHECK failed.
#pragma once

#include <cstdio>
#include <cstdint>
#include <vector>

namespace tf {

struct Stats { int checks = 0; int failures = 0; };
inline Stats &stats() { static Stats s; return s; }

using TestFn = void (*)();
struct Registry { std::vector<std::pair<const char *, TestFn>> tests; };
inline Registry &registry() { static Registry r; return r; }

struct Registrar {
  Registrar(const char *name, TestFn fn) { registry().tests.emplace_back(name, fn); }
};

inline int run_all() {
  for (auto &t : registry().tests) {
    std::fprintf(stderr, "[ RUN  ] %s\n", t.first);
    t.second();
  }
  std::fprintf(stderr, "----\n%d checks, %d failures\n",
               stats().checks, stats().failures);
  return stats().failures == 0 ? 0 : 1;
}

} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)

#define TEST(name)                                                            \
  static void TF_CAT(tf_test_, __LINE__)();                                   \
  static ::tf::Registrar TF_CAT(tf_reg_, __LINE__)(name,                      \
                                                   TF_CAT(tf_test_, __LINE__)); \
  static void TF_CAT(tf_test_, __LINE__)()

#define CHECK(cond)                                                           \
  do {                                                                        \
    ++::tf::stats().checks;                                                   \
    if (!(cond)) {                                                            \
      ++::tf::stats().failures;                                               \
      std::fprintf(stderr, "  FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__,   \
                   #cond);                                                    \
    }                                                                         \
  } while (0)

#define CHECK_EQ(a, b)                                                        \
  do {                                                                        \
    ++::tf::stats().checks;                                                   \
    auto tf_a = (a);                                                          \
    auto tf_b = (b);                                                          \
    if (!(tf_a == tf_b)) {                                                    \
      ++::tf::stats().failures;                                               \
      std::fprintf(stderr,                                                    \
                   "  FAIL %s:%d: CHECK_EQ(%s, %s)  (%lld != %lld)\n",        \
                   __FILE__, __LINE__, #a, #b, (long long)tf_a,              \
                   (long long)tf_b);                                          \
    }                                                                         \
  } while (0)

#define TEST_MAIN()                                                           \
  int main() { return ::tf::run_all(); }
