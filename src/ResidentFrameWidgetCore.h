#ifndef RESIDENT_FRAME_WIDGET_CORE_H
#define RESIDENT_FRAME_WIDGET_CORE_H

// The pure half of the frame widgets (ResidentFrameWidget.h): the fps range
// and the decision whether a draw makes a frame or redraws the last one. No
// LVGL, no Lua, so it is native-tested (test_frame_widget_core).

#include <cstdint>

namespace Resident {
namespace framewidget {

static constexpr uint8_t kDefaultFps = 15;
static constexpr uint8_t kMaxFps = 30;   // LVGL refreshes every 33 ms

inline uint8_t clampFps(long v) {
  if (v < 0) return 0;
  if (v > kMaxFps) return kMaxFps;
  return (uint8_t)v;
}

// Whether a draw makes a frame or redraws the last one. A partial-mode
// display draws a widget once per strip it crosses, every strip in the same
// refresh, so a frame is made only when one is owed (the fps timer, a
// changed key) or the pixels are new and blank; every later strip redraws
// what that draw made. LVGL's refresh is itself a timer, so neither the fps
// timer nor a handler owes a frame part-way through one: one refresh, one
// frame, one moment. (An fn that sets a key on its own widget would, and
// LVGL asserts on the invalidation that comes with it.)
struct FrameGate {
  bool due = true;

  void owe() { due = true; }

  // True when this draw makes the frame; it is then no longer owed.
  bool take(bool freshPixels) {
    const bool run = due || freshPixels;
    due = false;
    return run;
  }
};

}  // namespace framewidget
}  // namespace Resident

#endif  // RESIDENT_FRAME_WIDGET_CORE_H
