// The pure half of the Shader widget (ResidentShaderCore.h): the cell grid
// and the frame a shader function is called in, palettes, the rounded-corner
// mask, and the helpers (reference values computed independently, in Python).
#include <unity.h>
#include "ResidentShaderCore.h"

#include <vector>

using namespace Resident;
namespace f = shader::fn;

void setUp() {}
void tearDown() {}

// ── cells and frames ───────────────────────────────────────────────────────

void test_auto_cell_keeps_the_count_within_budget() {
  TEST_ASSERT_EQUAL(2, shader::cellFor(32, 64, 0));      // LED matrix: 16 x 32
  TEST_ASSERT_EQUAL(6, shader::cellFor(320, 172, 0));    // 1.47" TFT: 54 x 29
  TEST_ASSERT_EQUAL(9, shader::cellFor(360, 360, 0));    // 360 round: 40 x 40, exactly 1600
  TEST_ASSERT_EQUAL(12, shader::cellFor(468, 468, 0));   // 468 round: 39 x 39 (11 gives 43 x 43)
}

void test_a_smaller_budget_coarsens_the_grid() {
  TEST_ASSERT_TRUE(shader::cellFor(320, 172, 0, 400) > shader::cellFor(320, 172, 0));
}

void test_requested_cell_is_kept_and_capped() {
  TEST_ASSERT_EQUAL(2, shader::cellFor(320, 172, 2));
  TEST_ASSERT_EQUAL(shader::kMaxCell, shader::cellFor(320, 172, 99));
}

// The 12 strips of one refresh on a 468-row display draw one grid: a budget
// that auto mode shrank while the frame was made waits for the next owed
// frame.
void test_the_grid_holds_still_until_the_next_owed_frame() {
  const int shrunk = shader::kAutoCells * 2 / 3;
  TEST_ASSERT_EQUAL(12, shader::cellForDraw(false, true, 12, 468, 468, 0, shrunk));
  TEST_ASSERT_TRUE(shader::cellForDraw(true, true, 12, 468, 468, 0, shrunk) > 12);
}

void test_a_new_size_or_no_grid_takes_cell_for() {
  TEST_ASSERT_EQUAL(shader::cellFor(320, 172, 0), shader::cellForDraw(false, false, 12, 320, 172, 0));
  TEST_ASSERT_EQUAL(shader::cellFor(320, 172, 0), shader::cellForDraw(false, true, 0, 320, 172, 0));
  TEST_ASSERT_EQUAL(3, shader::cellForDraw(false, false, 5, 320, 172, 3));
}

// ── the auto-cell budget ───────────────────────────────────────────────────

// 40% of the frame period, capped at 25 ms; fps = 0 (e-paper) takes the cap.
void test_frame_budget_follows_fps() {
  TEST_ASSERT_EQUAL_UINT32(25000, shader::frameBudgetUs(0));
  TEST_ASSERT_EQUAL_UINT32(25000, shader::frameBudgetUs(-1));
  TEST_ASSERT_EQUAL_UINT32(25000, shader::frameBudgetUs(1));
  TEST_ASSERT_EQUAL_UINT32(25000, shader::frameBudgetUs(15));   // the default: 26.7 ms, capped
  TEST_ASSERT_EQUAL_UINT32(20000, shader::frameBudgetUs(20));
  TEST_ASSERT_EQUAL_UINT32(13333, shader::frameBudgetUs(30));   // the most: 40% of 33 ms
}

void test_cell_count_rounds_partial_cells_up() {
  TEST_ASSERT_EQUAL(1600, shader::cellCount(360, 360, 9));
  TEST_ASSERT_EQUAL(39 * 39, shader::cellCount(468, 468, 12));
  TEST_ASSERT_EQUAL(54 * 29, shader::cellCount(320, 172, 6));
}

// One frame over budget cuts the count to two thirds of what it ran, and the
// grid that count admits is coarser.
void test_a_slow_frame_coarsens() {
  shader::AutoBudget b;
  const int c = shader::cellFor(320, 172, 0, b.cells);
  b.frame(30000, 25000, 320, 172, c);
  TEST_ASSERT_EQUAL(shader::cellCount(320, 172, c) * 2 / 3, b.cells);
  TEST_ASSERT_TRUE(shader::cellFor(320, 172, 0, b.cells) > c);
}

// A frame within a 25 ms budget at 15 fps is over it at 30.
void test_the_same_frame_coarsens_at_a_higher_fps() {
  shader::AutoBudget at15, at30;
  at15.frame(20000, shader::frameBudgetUs(15), 320, 172, 6);
  at30.frame(20000, shader::frameBudgetUs(30), 320, 172, 6);
  TEST_ASSERT_EQUAL(shader::kAutoCells, at15.cells);
  TEST_ASSERT_TRUE(at30.cells < shader::kAutoCells);
}

// One slow frame (a GC step) does not coarsen the widget for good: a run of
// kRegrowFrames quick frames brings back the next finer edge, one at a time,
// and no further than where it started.
void test_quick_frames_regrow_after_a_spike() {
  const int w = 320, h = 172;
  shader::AutoBudget b;
  const int start = shader::cellFor(w, h, 0, b.cells);   // 6
  b.frame(40000, 25000, w, h, start);                     // the spike
  int c = shader::cellFor(w, h, 0, b.cells);
  TEST_ASSERT_TRUE(c > start);

  // A cheap fn: 4 us a cell, far inside the budget at any edge.
  for (int i = 0; i < 1000 && c > start; i++) {
    const int was = c;
    for (int f = 0; f < shader::kRegrowFrames - 1; f++) {
      b.frame(4u * shader::cellCount(w, h, c), 25000, w, h, c);
      TEST_ASSERT_EQUAL(was, shader::cellFor(w, h, 0, b.cells));   // not yet
    }
    b.frame(4u * shader::cellCount(w, h, c), 25000, w, h, c);
    c = shader::cellFor(w, h, 0, b.cells);
    TEST_ASSERT_EQUAL(was - 1, c);                                  // one edge finer
  }
  TEST_ASSERT_EQUAL(start, c);

  // At the start, the finer edge is past kAutoCells: it stays put.
  const int back = b.cells;
  TEST_ASSERT_TRUE(shader::cellCount(w, h, start - 1) > shader::kAutoCells);
  for (int f = 0; f < shader::kRegrowFrames * 3; f++) b.frame(1000, 25000, w, h, c);
  TEST_ASSERT_EQUAL(back, b.cells);
  TEST_ASSERT_EQUAL(start, shader::cellFor(w, h, 0, b.cells));
}

// A frame whose time per cell predicts the finer grid would take more than
// three quarters of the budget does not count towards regrowing, and breaks
// the run: a steadily heavy fn holds its edge rather than oscillating.
void test_no_regrow_unless_the_finer_grid_fits_with_margin() {
  const int w = 320, h = 172, c = 8;
  const int now = shader::cellCount(w, h, c), finer = shader::cellCount(w, h, c - 1);
  shader::AutoBudget b;
  b.cells = now;
  // Just over the line: finer would take 76% of the budget.
  const uint32_t heavy = (uint32_t)(25000ull * 76 / 100 * now / finer);
  for (int f = 0; f < shader::kRegrowFrames * 3; f++) b.frame(heavy, 25000, w, h, c);
  TEST_ASSERT_EQUAL(now, b.cells);

  // kRegrowFrames - 1 quick frames, then one heavy: the run starts again.
  const uint32_t light = (uint32_t)(25000ull * 50 / 100 * now / finer);
  for (int f = 0; f < shader::kRegrowFrames - 1; f++) b.frame(light, 25000, w, h, c);
  b.frame(heavy, 25000, w, h, c);
  for (int f = 0; f < shader::kRegrowFrames - 1; f++) b.frame(light, 25000, w, h, c);
  TEST_ASSERT_EQUAL(now, b.cells);
  b.frame(light, 25000, w, h, c);
  TEST_ASSERT_EQUAL(finer, b.cells);
}

// At the coarsest edge an overrun has nowhere to go; at edge 1 there is no
// finer grid. Neither changes the count, and reset() starts over.
void test_budget_edges_and_reset() {
  shader::AutoBudget b;
  b.cells = 100;
  b.frame(99999, 25000, 320, 172, shader::kMaxCell);
  TEST_ASSERT_EQUAL(100, b.cells);
  for (int f = 0; f < shader::kRegrowFrames * 2; f++) b.frame(1, 25000, 8, 8, 1);
  TEST_ASSERT_EQUAL(100, b.cells);
  b.frame(1, 25000, 320, 172, 0);   // no grid yet: ignored
  TEST_ASSERT_EQUAL(100, b.cells);
  b.calm = 7;
  b.reset();
  TEST_ASSERT_EQUAL(shader::kAutoCells, b.cells);
  TEST_ASSERT_EQUAL(0, b.calm);
}

void test_frame_centres_the_shorter_side_on_minus_one_to_one() {
  // 200 x 100 at cell 1: y spans -1..1 (shorter side), x spans -2..2, y up.
  const shader::Frame fr = shader::frameFor(200, 100, 1, 1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4, -2.0f + 0.01f, fr.x0);          // first column's centre
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.0f - 0.01f, fr.y0);           // top row is +y
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 2.0f - 0.01f, fr.x0 + 199 * fr.xStep);
  TEST_ASSERT_FLOAT_WITHIN(1e-4, -1.0f + 0.01f, fr.y0 + 99 * fr.yStep);
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.0025f, fr.u0);                // u, v: 0..1, v up
  TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.995f, fr.v0);
}

void test_frame_scale_zooms_in() {
  const shader::Frame a = shader::frameFor(100, 100, 1, 1.0f);
  const shader::Frame b = shader::frameFor(100, 100, 1, 2.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, a.xStep / 2, b.xStep);
}

// ── palettes ───────────────────────────────────────────────────────────────

void test_palette_spans_its_stops() {
  shader::Palette p;
  TEST_ASSERT_TRUE(shader::namedPalette("mono", p));
  TEST_ASSERT_EQUAL_HEX16(0x0000, p.lut[0]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, p.lut[255]);
  TEST_ASSERT_EQUAL_HEX16(0x8410, p.lut[128]);
}

void test_custom_palette_and_its_limits() {
  shader::Rgb stops[3] = {{0, 0, 0}, {255, 0, 0}, {0, 0, 255}};
  shader::Palette p;
  TEST_ASSERT_TRUE(shader::buildPalette(stops, 3, p));
  TEST_ASSERT_EQUAL_HEX16(0xF800, p.lut[128] & 0xF800);
  TEST_ASSERT_EQUAL_HEX16(0x001F, p.lut[255]);
  TEST_ASSERT_FALSE(shader::buildPalette(stops, 1, p));
  TEST_ASSERT_FALSE(shader::namedPalette("puce", p));
  for (int i = 0; i < shader::kPaletteCount; i++) {
    TEST_ASSERT_TRUE(shader::namedPalette(shader::kPalettes[i].name, p));
  }
}

void test_unit8_clamps_and_survives_nan() {
  TEST_ASSERT_EQUAL(0, shader::unit8(-1));
  TEST_ASSERT_EQUAL(255, shader::unit8(7));
  TEST_ASSERT_EQUAL(128, shader::unit8(0.5f));
  TEST_ASSERT_EQUAL(0, shader::unit8(NAN));
}

// ── helpers ────────────────────────────────────────────────────────────────

void test_glsl_scalars() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.25f, f::fract(3.25f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.75f, f::fract(-0.25f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 2.5f, f::mix(2, 3, 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.0f, f::clamp(4, 0, 1));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.0f, f::step(0.5f, 0.4f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.0f, f::step(0.5f, 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.15625f, f::smoothstep(0, 1, 0.25f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.5f, f::mod(-1.5f, 2));
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 5.0f, f::length(3, 4));
}

void test_hash_and_noise_stay_in_range_and_are_continuous() {
  float lo = 1, hi = 0;
  for (int i = 0; i < 2000; i++) {
    const float x = i * 0.137f - 40, y = i * 0.071f + 3;
    const float h = f::hash(x, y), n = f::noise(x, y), b = f::fbm(x, y);
    TEST_ASSERT_TRUE(h >= 0 && h <= 1);
    TEST_ASSERT_TRUE(n >= 0 && n <= 1);
    TEST_ASSERT_TRUE(b >= 0 && b <= 1);
    if (n < lo) lo = n;
    if (n > hi) hi = n;
  }
  TEST_ASSERT_TRUE(hi - lo > 0.6f);   // it varies
  // Continuous: a tiny step moves noise a tiny amount (hash is a step function).
  TEST_ASSERT_FLOAT_WITHIN(0.01f, f::noise(1.5f, 2.5f), f::noise(1.5001f, 2.5f));
  TEST_ASSERT_EQUAL_FLOAT(f::hash(1.2f, 2.9f), f::hash(1.8f, 2.1f));   // same unit square
}

void test_fbm_octaves_are_clamped_and_deterministic() {
  // Pinned (computed by running this implementation): a change to the octave
  // turn, offsets or weights moves it.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.442483f, f::fbm(0.3f, 0.7f, 4));
  TEST_ASSERT_EQUAL_FLOAT(f::fbm(0.3f, 0.7f, 8), f::fbm(0.3f, 0.7f, 99));
  TEST_ASSERT_EQUAL_FLOAT(f::noise(0.3f, 0.7f), f::fbm(0.3f, 0.7f, 1));
}

void test_hsv() {
  float r, g, b;
  f::hsv(1.0f / 3, 1, 1, r, g, b);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0, r);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 1, g);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0, b);
  f::hsv(1.5f, 0.5f, 0.8f, r, g, b);   // hue wraps: as 0.5
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.4f, r);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.8f, g);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.8f, b);
}

void test_signed_distances() {
  TEST_ASSERT_FLOAT_WITHIN(1e-5, -0.5f, f::circle(0, 0, 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.5f, f::circle(1, 0, 0.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.0f, f::box(2, 0, 1, 1));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, -1.0f, f::box(0, 0, 1, 1));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.41421356f, f::box(2, 2, 1, 1));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.0f, f::segment(0, 1, -1, 0, 1, 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 2.0f, f::segment(3, 0, -1, 0, 1, 0));
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.0f, f::segment(0, 1, 0, 0, 0, 0));   // a point
}

void test_smin_is_below_both_and_hard_at_zero() {
  TEST_ASSERT_TRUE(f::smin(0.2f, 0.25f, 0.3f) < 0.2f);
  TEST_ASSERT_EQUAL_FLOAT(0.2f, f::smin(0.2f, 0.25f, 0));
  TEST_ASSERT_EQUAL_FLOAT(0.2f, f::smin(0.2f, 5.0f, 0.1f));   // far apart: just the min
}

void test_rot_quarter_turn() {
  float x, y;
  f::rot(1, 0, 1.57079632679f, x, y);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 0, x);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 1, y);
}

// ── corners ────────────────────────────────────────────────────────────────

void test_corner_mask_is_a_circle_at_a_huge_radius() {
  std::vector<uint8_t> a(20 * 20);
  shader::cornerMask(20, 20, 0x7FFF, a.data(), 20);
  TEST_ASSERT_EQUAL(0, a[0]);
  TEST_ASSERT_EQUAL(255, a[10 * 20 + 10]);
  TEST_ASSERT_EQUAL(255, a[10 * 20 + 1]);
  TEST_ASSERT_EQUAL(0, a[2 * 20 + 2]);
}

void test_corner_mask_radius_zero_is_all_in_and_honours_stride() {
  std::vector<uint8_t> a(8 * 4, 7);
  shader::cornerMask(8, 4, 0, a.data(), 8);
  for (uint8_t v : a) TEST_ASSERT_EQUAL(255, v);
  std::vector<uint8_t> b(12 * 10, 9);
  shader::cornerMask(10, 10, 3, b.data(), 12);
  TEST_ASSERT_EQUAL(9, b[10]);
  TEST_ASSERT_EQUAL(9, b[11]);
  TEST_ASSERT_EQUAL(255, b[5 * 12 + 5]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_auto_cell_keeps_the_count_within_budget);
  RUN_TEST(test_a_smaller_budget_coarsens_the_grid);
  RUN_TEST(test_requested_cell_is_kept_and_capped);
  RUN_TEST(test_the_grid_holds_still_until_the_next_owed_frame);
  RUN_TEST(test_a_new_size_or_no_grid_takes_cell_for);
  RUN_TEST(test_frame_budget_follows_fps);
  RUN_TEST(test_cell_count_rounds_partial_cells_up);
  RUN_TEST(test_a_slow_frame_coarsens);
  RUN_TEST(test_the_same_frame_coarsens_at_a_higher_fps);
  RUN_TEST(test_quick_frames_regrow_after_a_spike);
  RUN_TEST(test_no_regrow_unless_the_finer_grid_fits_with_margin);
  RUN_TEST(test_budget_edges_and_reset);
  RUN_TEST(test_frame_centres_the_shorter_side_on_minus_one_to_one);
  RUN_TEST(test_frame_scale_zooms_in);
  RUN_TEST(test_palette_spans_its_stops);
  RUN_TEST(test_custom_palette_and_its_limits);
  RUN_TEST(test_unit8_clamps_and_survives_nan);
  RUN_TEST(test_glsl_scalars);
  RUN_TEST(test_hash_and_noise_stay_in_range_and_are_continuous);
  RUN_TEST(test_fbm_octaves_are_clamped_and_deterministic);
  RUN_TEST(test_hsv);
  RUN_TEST(test_signed_distances);
  RUN_TEST(test_smin_is_below_both_and_hard_at_zero);
  RUN_TEST(test_rot_quarter_turn);
  RUN_TEST(test_corner_mask_is_a_circle_at_a_huge_radius);
  RUN_TEST(test_corner_mask_radius_zero_is_all_in_and_honours_stride);
  return UNITY_END();
}
