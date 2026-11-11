// fasttrig::sin / cos (ResidentFastTrig.h) against the double-precision
// functions: within two units in the last place over the range they reduce
// themselves (half from rounding the remainder to a float, the rest libm's
// own; 1.5 is the worst seen), and exactly libm's outside it.
#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ResidentFastTrig.h"

using namespace Resident;

namespace {
uint32_t g_rng = 0x9e3779b9u;
uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

// The distance from `got` to the true value, in units of the last place of
// the true value's float.
double ulps(float got, double truth) {
  const float t = (float)truth;
  int e;
  std::frexp(t == 0 ? 1e-30f : t, &e);
  const double ulp = std::ldexp(1.0, e - 24);
  return std::fabs((double)got - truth) / ulp;
}
}  // namespace

void setUp() {}
void tearDown() {}

void test_small_arguments_are_libms() {
  for (float x = -199.5f; x < 199.5f; x += 0.37f) {
    TEST_ASSERT_EQUAL_FLOAT(std::sin(x), fasttrig::sin(x));
    TEST_ASSERT_EQUAL_FLOAT(std::cos(x), fasttrig::cos(x));
    const float a = std::sin(x), b = fasttrig::sin(x);
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(a));
  }
}

void test_large_arguments_are_within_two_ulp() {
  double worstS = 0, worstC = 0;
  for (int i = 0; i < 400000; i++) {
    // Spread over 200 .. 2^20, either sign.
    const float mag = 200.0f + (float)(rnd() % 1048000u) + (float)(rnd() & 0xFFFF) / 65536.0f;
    const float x = (rnd() & 1) ? mag : -mag;
    if (std::fabs(x) > fasttrig::kMax) continue;
    const double s = ulps(fasttrig::sin(x), std::sin((double)x));
    const double c = ulps(fasttrig::cos(x), std::cos((double)x));
    if (s > worstS) worstS = s;
    if (c > worstC) worstC = c;
  }
  char msg[80];
  snprintf(msg, sizeof(msg), "worst sin %.3f ulp, cos %.3f ulp", worstS, worstC);
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE_MESSAGE(worstS <= 2.0, "sin is more than 2 ulp out");
  TEST_ASSERT_TRUE_MESSAGE(worstC <= 2.0, "cos is more than 2 ulp out");
}

void test_the_shader_hash_idiom() {
  // fract(sin(n * 12.9898) * 43758.5453): the argument is large from n = 16.
  for (int n = 16; n < 20000; n++) {
    const float x = (float)n * 12.9898f;
    TEST_ASSERT_TRUE(ulps(fasttrig::sin(x), std::sin((double)x)) <= 2.0);
  }
}

void test_beyond_the_range_and_not_a_number() {
  const float huge = 3.0e7f;
  TEST_ASSERT_EQUAL_FLOAT(std::sin(huge), fasttrig::sin(huge));
  TEST_ASSERT_EQUAL_FLOAT(std::cos(huge), fasttrig::cos(huge));
  TEST_ASSERT_TRUE(std::isnan(fasttrig::sin(NAN)));
  TEST_ASSERT_TRUE(std::isnan(fasttrig::cos(INFINITY)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_small_arguments_are_libms);
  RUN_TEST(test_large_arguments_are_within_two_ulp);
  RUN_TEST(test_the_shader_hash_idiom);
  RUN_TEST(test_beyond_the_range_and_not_a_number);
  return UNITY_END();
}
