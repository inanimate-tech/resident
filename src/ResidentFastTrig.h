#pragma once

// sin and cos of a large single-precision argument, without the slow path.
//
// newlib's sinf and cosf reduce an argument below about 201 (2^7 * pi/2) in
// a few float operations, and anything larger through __kernel_rem_pio2f, a
// multi-word reduction built for arguments of any size. On an ESP32-S3 that
// is 17 us a call against 0.8 us (measured, 2026-11). Lua apps
// reach it constantly without meaning to: an animation's phase is seconds
// times a speed, which passes 201 half a minute after the app loads, and the
// shader idiom fract(sin(n * 12.9898) * 43758.5453) passes it at n = 16.
//
// Below the threshold these are libm's own. Above it (up to 2^20, past which
// libm takes over again) the argument is reduced to within pi/4 of a multiple
// of pi/2 in double precision — a handful of software-double operations,
// which is still several times cheaper — and libm evaluates the remainder.
// The double carries the reduction to well below a float's last place; the
// remainder is then rounded to a float, which with libm's own error keeps
// the result within two units in the last place of the true value (1.5 is the
// worst the test finds).
//
// Pure, so the bound is tested natively (test/unit/test/test_fast_trig).

#include <cmath>
#include <cstdint>

namespace Resident {
namespace fasttrig {

static constexpr float kLibmFast = 200.0f;      // libm's own quick reduction covers this
static constexpr float kMax = 1048576.0f;       // 2^20: beyond, leave it to libm

// x = n * pi/2 + r, |r| <= pi/4. Returns r; `quadrant` is n mod 4.
inline float reduce(float x, int& quadrant) {
  const double xd = (double)x;
  const double n = std::nearbyint(xd * 0.63661977236758134308);   // 2/pi
  // pi/2 in two parts, so n * (its high part) is exact for |n| < 2^20.
  const double r = (xd - n * 1.57079632673412561417e+00) - n * 6.07710050650619224932e-11;
  quadrant = (int)((int64_t)n & 3);
  return (float)r;
}

inline float sin(float x) {
  const float ax = std::fabs(x);
  if (!(ax >= kLibmFast) || ax > kMax) return std::sin(x);   // small, NaN, or huge
  int q;
  const float r = reduce(x, q);
  switch (q) {
    case 0: return std::sin(r);
    case 1: return std::cos(r);
    case 2: return -std::sin(r);
    default: return -std::cos(r);
  }
}

inline float cos(float x) {
  const float ax = std::fabs(x);
  if (!(ax >= kLibmFast) || ax > kMax) return std::cos(x);
  int q;
  const float r = reduce(x, q);
  switch (q) {
    case 0: return std::cos(r);
    case 1: return -std::sin(r);
    case 2: return -std::cos(r);
    default: return std::sin(r);
  }
}

}  // namespace fasttrig
}  // namespace Resident
