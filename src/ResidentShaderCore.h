#ifndef RESIDENT_SHADER_CORE_H
#define RESIDENT_SHADER_CORE_H

// The pure half of the Shader widget (ResidentShaderWidget.h): the cell
// grid, palettes, rounded corners, and the helpers a shader function calls.
//
// A Shader's program is a Lua function the app writes, so it hot-reloads
// like the rest of the app (a live chunk replaces it). C++ owns everything
// around that call: the loop over the cells, the palette, the blocks, the
// corners and the blit — and the helpers below, which are the arithmetic a
// shader spends its time in, done natively.
//
// The function is called once per CELL, not per pixel: the widget divides
// itself into cell x cell blocks and paints each block one colour. A Lua
// call costs microseconds on an ESP32, so the grid starts at about
// kAutoCells cells and the widget coarsens it when a frame runs over budget
// (see ResidentShaderWidget.h) — on a 32x64 LED matrix or a 256x48 strip the
// chunky cell is the look anyway.
//
// The helpers are Shadertoy's everyday kit, as scalars (Lua has no vec2):
// GLSL's fract/mix/clamp/step/smoothstep/mod/length, a hash, value noise
// and fbm, hsv -> rgb, Inigo Quilez's 2D signed distances (circle, box,
// segment) and smooth minimum, and a 2D rotation.
//
// Dependency-free so it is tested natively (test_shader_core).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Resident {
namespace shader {

// About this many cells in a widget when the app leaves `cell` to the
// widget: a Lua call each, so a few thousand, not tens of thousands.
static constexpr int kAutoCells = 1600;
static constexpr int kMaxCell = 16;

// The cell edge for a w x h widget: the smallest that keeps the count at or
// under `budget`, or the app's own (1..kMaxCell).
inline int cellFor(int w, int h, int requested, int budget = kAutoCells) {
  if (requested >= 1) return requested > kMaxCell ? kMaxCell : requested;
  for (int c = 1; c < kMaxCell; c++) {
    const int cw = (w + c - 1) / c, ch = (h + c - 1) / c;
    if (cw * ch <= budget) return c;
  }
  return kMaxCell;
}

// The cell edge for one draw: cellFor's while a frame is owed or there is no
// grid of this widget's size, otherwise the edge the current frame was made
// at. A partial-mode display draws a widget in several strips per refresh,
// and a budget that shrank while the frame was made waits for the next owed
// frame rather than resizing the grid under a later strip.
inline int cellForDraw(bool owed, bool sameSize, int cellNow, int w, int h, int requested,
                       int budget = kAutoCells) {
  if (owed || !sameSize || cellNow <= 0) return cellFor(w, h, requested, budget);
  return cellNow;
}

// ── palettes ───────────────────────────────────────────────────────────────

struct Rgb { uint8_t r, g, b; };

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

inline uint8_t unit8(float v) {
  if (!(v > 0)) return 0;          // also NaN
  if (v >= 1) return 255;
  return (uint8_t)(v * 255.0f + 0.5f);
}

// A value 0..1 -> RGB565, through 256 entries built from 2..8 stops spaced
// evenly (stop 0 at 0, the last at 1).
struct Palette {
  uint16_t lut[256] = {};
  uint8_t count = 0;
};

static constexpr int kMaxStops = 8;

inline bool buildPalette(const Rgb* stops, int n, Palette& out) {
  if (n < 2 || n > kMaxStops) return false;
  out.count = (uint8_t)n;
  for (int v = 0; v < 256; v++) {
    const float pos = v * (n - 1) / 255.0f;
    int i = (int)pos;
    if (i >= n - 1) i = n - 2;
    const float f = pos - i;
    const Rgb& a = stops[i];
    const Rgb& b = stops[i + 1];
    out.lut[v] = rgb565((uint8_t)(a.r + (b.r - a.r) * f + 0.5f),
                        (uint8_t)(a.g + (b.g - a.g) * f + 0.5f),
                        (uint8_t)(a.b + (b.b - a.b) * f + 0.5f));
  }
  return true;
}

struct NamedPalette { const char* name; uint8_t n; Rgb stops[kMaxStops]; };

// Dark at 0, light at 1, so a field that rests low reads as a near-black
// ground.
static constexpr NamedPalette kPalettes[] = {
  {"ember",   5, {{0, 0, 0}, {96, 8, 0}, {220, 48, 0}, {255, 168, 24}, {255, 248, 200}}},
  {"ocean",   5, {{0, 4, 16}, {0, 32, 96}, {0, 120, 160}, {64, 210, 220}, {230, 255, 255}}},
  {"aurora",  5, {{4, 0, 20}, {40, 0, 90}, {0, 140, 120}, {80, 255, 140}, {220, 255, 240}}},
  {"sunset",  5, {{10, 0, 30}, {110, 0, 90}, {230, 40, 70}, {255, 150, 40}, {255, 240, 160}}},
  {"rainbow", 7, {{255, 0, 0}, {255, 160, 0}, {240, 240, 0}, {0, 220, 0}, {0, 160, 255}, {80, 0, 255}, {255, 0, 160}}},
  {"mono",    2, {{0, 0, 0}, {255, 255, 255}}},
};
static constexpr int kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

inline bool namedPalette(const char* name, Palette& out) {
  if (!name) return false;
  for (const auto& p : kPalettes) {
    if (std::strcmp(name, p.name) == 0) return buildPalette(p.stops, p.n, out);
  }
  return false;
}

// ── rounded corners ────────────────────────────────────────────────────────

// The coverage of a w x h rounded rectangle of radius r, one byte a pixel
// (255 inside, 0 outside, antialiased on the curve), into `alpha` with the
// given row stride. r is clamped to half the shorter side, so a huge radius
// (lvgl.RADIUS_CIRCLE) is a circle or a pill. LVGL's software renderer
// ignores an image's clip_radius, so the widget carries its corners in its
// own alpha plane, computed only when the size or radius changes.
inline void cornerMask(int w, int h, int r, uint8_t* alpha, int stride) {
  const int maxR = (w < h ? w : h) / 2;
  if (r > maxR) r = maxR;
  if (r < 0) r = 0;
  for (int y = 0; y < h; y++) {
    uint8_t* row = alpha + (size_t)y * stride;
    for (int x = 0; x < w; x++) {
      const float px = x + 0.5f, py = y + 0.5f;
      float dx = 0, dy = 0;
      if (px < r) dx = r - px; else if (px > w - r) dx = px - (w - r);
      if (py < r) dy = r - py; else if (py > h - r) dy = py - (h - r);
      if (dx == 0 || dy == 0) { row[x] = 255; continue; }
      const float cover = r - std::sqrt(dx * dx + dy * dy) + 0.5f;
      row[x] = cover <= 0 ? 0 : cover >= 1 ? 255 : (uint8_t)(cover * 255.0f + 0.5f);
    }
  }
}

// ── coordinates ────────────────────────────────────────────────────────────

// Where a cell's centre sits, in the two frames a shader function is given.
// x, y: centred, the SHORTER side spanning -1..1 (Shadertoy's
// p = (2*fragCoord - iResolution.xy) / iResolution.y, but on the shorter
// side so a strip and a square agree), y UP, times 1/scale. u, v: 0..1
// across the widget, v up.
struct Frame {
  float x0, xStep, y0, yStep;   // x = x0 + col * xStep, y = y0 + row * yStep
  float u0, uStep, v0, vStep;
};

inline Frame frameFor(int w, int h, int cell, float scale) {
  const float s = 2.0f / (float)(w < h ? w : h) / (scale > 0 ? scale : 1.0f);
  Frame f;
  f.xStep = cell * s;
  f.x0 = (cell * 0.5f - w * 0.5f) * s;
  f.yStep = -cell * s;
  f.y0 = (h * 0.5f - cell * 0.5f) * s;
  f.uStep = (float)cell / w;
  f.u0 = cell * 0.5f / w;
  f.vStep = -(float)cell / h;
  f.v0 = 1.0f - cell * 0.5f / h;
  return f;
}

// ── the helpers ────────────────────────────────────────────────────────────

namespace fn {

inline float fract(float x) { return x - std::floor(x); }
inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline float clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float step(float edge, float x) { return x < edge ? 0.0f : 1.0f; }
inline float smoothstep(float e0, float e1, float x) {
  const float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// GLSL's mod: the result takes the sign of y.
inline float mod(float x, float y) { return x - y * std::floor(x / y); }
inline float length(float x, float y) { return std::sqrt(x * x + y * y); }

inline uint32_t hash32(int32_t x, int32_t y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 0x27d4eb2dU ^ (uint32_t)y * 0x165667b1U ^ (seed + 0x9e3779b9U) * 0x85ebca6bU;
  h ^= h >> 15; h *= 0x2c1b3c6dU; h ^= h >> 12; h *= 0x297a2d39U; h ^= h >> 15;
  return h;
}

// 0..1, constant over each unit square.
inline float hash(float x, float y) {
  return (hash32((int32_t)std::floor(x), (int32_t)std::floor(y), 0) & 0xFFFFFF) / 16777215.0f;
}

// Value noise, 0..1, smooth: the hash at the integer lattice, eased between.
inline float noise(float x, float y) {
  const float fx = std::floor(x), fy = std::floor(y);
  const int32_t ix = (int32_t)fx, iy = (int32_t)fy;
  float tx = x - fx, ty = y - fy;
  tx = tx * tx * (3.0f - 2.0f * tx);
  ty = ty * ty * (3.0f - 2.0f * ty);
  const float a = (hash32(ix, iy, 0) & 0xFFFF) / 65535.0f;
  const float b = (hash32(ix + 1, iy, 0) & 0xFFFF) / 65535.0f;
  const float c = (hash32(ix, iy + 1, 0) & 0xFFFF) / 65535.0f;
  const float d = (hash32(ix + 1, iy + 1, 0) & 0xFFFF) / 65535.0f;
  const float top = a + (b - a) * tx, bottom = c + (d - c) * tx;
  return top + (bottom - top) * ty;
}

// Fractal noise: `octaves` (1..8, default 4) of noise, each twice the
// frequency and half the amplitude, normalised to 0..1.
inline float fbm(float x, float y, int octaves = 4) {
  if (octaves < 1) octaves = 1;
  if (octaves > 8) octaves = 8;
  float sum = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < octaves; o++) {
    sum += amp * noise(x, y);
    norm += amp;
    // Turn a little each octave so the lattices do not line up.
    const float nx = 1.6f * x + 1.2f * y, ny = -1.2f * x + 1.6f * y;
    x = nx + 17.0f; y = ny + 31.0f;
    amp *= 0.5f;
  }
  return sum / norm;
}

// h, s, v in 0..1 (h wraps) -> r, g, b in 0..1.
inline void hsv(float h, float s, float v, float& r, float& g, float& b) {
  h = fract(h) * 6.0f;
  s = clamp(s, 0, 1);
  v = clamp(v, 0, 1);
  const int i = (int)h;
  const float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

// Signed distances (negative inside), Inigo Quilez's 2D set.
inline float circle(float x, float y, float r) { return length(x, y) - r; }
// A box of half-size (hw, hh), centred.
inline float box(float x, float y, float hw, float hh) {
  const float dx = std::fabs(x) - hw, dy = std::fabs(y) - hh;
  const float ox = dx > 0 ? dx : 0, oy = dy > 0 ? dy : 0;
  const float inside = dx > dy ? dx : dy;
  return length(ox, oy) + (inside < 0 ? inside : 0);
}
// Distance to the segment (ax, ay)-(bx, by); subtract a radius for a stroke.
inline float segment(float x, float y, float ax, float ay, float bx, float by) {
  const float pax = x - ax, pay = y - ay, bax = bx - ax, bay = by - ay;
  const float d = bax * bax + bay * bay;
  const float h = d > 0 ? clamp((pax * bax + pay * bay) / d, 0, 1) : 0;
  return length(pax - bax * h, pay - bay * h);
}
// Smooth minimum: a union of two distances with a fillet of size k.
inline float smin(float a, float b, float k) {
  if (k <= 0) return a < b ? a : b;
  const float h = clamp(0.5f + 0.5f * (b - a) / k, 0, 1);
  return mix(b, a, h) - k * h * (1 - h);
}
// (x, y) rotated by angle a (radians, anticlockwise).
inline void rot(float x, float y, float a, float& ox, float& oy) {
  const float c = std::cos(a), s = std::sin(a);
  ox = c * x - s * y;
  oy = s * x + c * y;
}

}  // namespace fn
}  // namespace shader
}  // namespace Resident

#endif  // RESIDENT_SHADER_CORE_H
