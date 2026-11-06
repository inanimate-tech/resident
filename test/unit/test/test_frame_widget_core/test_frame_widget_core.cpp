// The pure half of the frame widgets (ResidentFrameWidgetCore.h), the base
// of Shader and Lgfx: the fps clamp, and the gate that makes one frame per
// owed frame however many strips a partial-mode display draws it in.
#include <unity.h>
#include "ResidentFrameWidgetCore.h"

using namespace Resident;

void setUp() {}
void tearDown() {}

void test_fps_is_clamped_to_zero_through_thirty() {
  TEST_ASSERT_EQUAL(0, framewidget::clampFps(-4));
  TEST_ASSERT_EQUAL(0, framewidget::clampFps(0));
  TEST_ASSERT_EQUAL(20, framewidget::clampFps(20));
  TEST_ASSERT_EQUAL(30, framewidget::clampFps(60));
}

// A 468-row display with 40-row draw buffers draws a full-screen widget in
// 12 strips per refresh: the first makes the frame, the rest redraw it.
void test_one_frame_per_owed_frame_across_strips() {
  framewidget::FrameGate g;
  int frames = 0;
  for (int strip = 0; strip < 12; strip++) {
    if (g.take(false)) frames++;
  }
  TEST_ASSERT_EQUAL(1, frames);

  // Nothing owed: a redraw (another widget invalidating over it) reuses.
  TEST_ASSERT_FALSE(g.take(false));

  g.owe();
  TEST_ASSERT_TRUE(g.take(false));
  TEST_ASSERT_FALSE(g.take(false));
}

void test_new_pixels_force_a_frame() {
  framewidget::FrameGate g;
  TEST_ASSERT_TRUE(g.take(false));    // a new widget owes its first frame
  TEST_ASSERT_FALSE(g.take(false));
  TEST_ASSERT_TRUE(g.take(true));     // buffers remade: their pixels are blank
  TEST_ASSERT_FALSE(g.take(false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fps_is_clamped_to_zero_through_thirty);
  RUN_TEST(test_one_frame_per_owed_frame_across_strips);
  RUN_TEST(test_new_pixels_force_a_frame);
  return UNITY_END();
}
