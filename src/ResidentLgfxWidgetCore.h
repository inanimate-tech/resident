#ifndef RESIDENT_LGFX_WIDGET_CORE_H
#define RESIDENT_LGFX_WIDGET_CORE_H

// The pure half of the Lgfx widget (ResidentLgfxWidget.h): what a Lua
// argument means (a coordinate, a colour) and how a sprite's pixels become
// LVGL's. No LVGL, no Lua, no LovyanGFX, so it is native-tested
// (test_lgfx_widget_core).

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Resident {
namespace lgfxw {

// A coordinate: Lua numbers are floats as often as not (a sine, a
// projection), and drawing is on whole pixels, so round to nearest rather
// than refuse a fraction as the lgfx module does. Out-of-range values clamp
// rather than wrap, so a point far off the panel stays off it.
inline int32_t px(double v) {
  if (!(v == v)) return 0;   // NaN
  if (v > 1e6) return 1000000;
  if (v < -1e6) return -1000000;
  return (int32_t)std::floor(v + 0.5);
}

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "#RRGGBB" or "#RGB" -> 0xRRGGBB. false for anything else.
inline bool parseHex(const char* s, uint32_t& out) {
  if (!s || s[0] != '#') return false;
  size_t n = 0;
  while (s[1 + n]) n++;
  if (n != 6 && n != 3) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < n; i++) {
    const int d = hexDigit(s[1 + i]);
    if (d < 0) return false;
    if (n == 3) v = (v << 8) | (uint32_t)(d * 17);
    else v = (v << 4) | (uint32_t)d;
  }
  out = v;
  return true;
}

// LovyanGFX's 16-bit sprite holds RGB565 byte-swapped (the panel's wire
// order); LVGL's RGB565 is native little-endian. One pass, swapping.
inline void copySwapped(const uint16_t* src, uint16_t* dst, size_t n) {
  for (size_t i = 0; i < n; i++) dst[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
}

// 0xRRGGBB -> RGB565 in LVGL's order, for comparing against a copied pixel.
inline uint16_t rgb565(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

// The same copy for a widget with a transparent colour: every pixel that is
// the key gets alpha 0 in the RGB565A8 alpha plane, the rest 255.
inline void copySwappedKeyed(const uint16_t* src, uint16_t* dst, uint8_t* alpha, size_t n,
                             uint16_t key) {
  for (size_t i = 0; i < n; i++) {
    const uint16_t v = (uint16_t)((src[i] >> 8) | (src[i] << 8));
    dst[i] = v;
    alpha[i] = v == key ? 0 : 255;
  }
}

}  // namespace lgfxw
}  // namespace Resident

#endif  // RESIDENT_LGFX_WIDGET_CORE_H
