// src/ResidentSystemLayer.h — when the firmware's LVGL system layer may draw.
//
// LVGL gives every display a system layer (lv_layer_sys): objects above every
// screen of that display, composed into the same frame and redrawn only where
// they change. LvglModule hands it to firmware (LvglModule::systemLayer), so a
// board or a platform can draw above whatever app is running — a status strip,
// a voice indicator — without an overlay claim and without suspending the app.
// Apps cannot reach it: the module hides it from Lua.
//
// This header is the one decision that needs care, kept pure so it is
// native-tested (test/unit/test/test_system_layer): for one display, given who
// owns the target and whether the layer has anything showing, may LVGL render,
// may its frames reach the glass, and does the content underneath need putting
// back.
//
//   * An app owns the target (lvgl.bind): LVGL renders the app and the layer as
//     one frame — exactly as without a system layer.
//   * Nobody owns it (no app, or between apps): the glass shows whatever the
//     board's own driver last drew there (boot text, an idle screen). LVGL
//     renders, and flushes, only while the layer has something visible; it
//     redraws only what the layer invalidates, so it draws the layer over the
//     driver's content rather than a blank screen. When the layer empties
//     again, the board's driver repaints its content (restoreContent), because
//     LVGL's last frame there was its own empty screen under the layer.
//   * Another library owns it (lgfx.bind): the layer stands down entirely.
//   * The app is suspended (an overlay claim on a dual-role surface): the board
//     suppresses its blit door, so nothing of LVGL's may render — a frame
//     LVGL believes reached the glass would be lost (the 0.11 fix). The layer
//     waits with the app.
//   * A system-only display (LvglModule::addSystemDisplay) has no app and no
//     other library: the layer is all there is, and always draws.
#ifndef RESIDENT_SYSTEM_LAYER_H
#define RESIDENT_SYSTEM_LAYER_H

#include <cstdint>

namespace Resident {

struct SystemLayerGate {
  enum class Owner : uint8_t { None, Lvgl, Other };

  struct Inputs {
    Owner owner = Owner::None;
    bool systemOnly = false;  // a display only the firmware draws on
    bool visible = false;     // the system layer has a visible child
    bool suspended = false;   // the app is suspended (its surface is claimed)
  };

  struct Decision {
    bool render = false;   // LVGL's refresh timer for this display runs
    bool flush = false;    // a rendered area may reach the panel
    bool restore = false;  // ask the board's driver to repaint what is underneath
  };

  // `drewOverDriver` is the gate's one piece of memory: the layer has drawn
  // over a target nobody owned, so the driver's content there is no longer on
  // the glass. Owned by the caller, one per display; decide() updates it.
  static Decision decide(const Inputs& in, bool& drewOverDriver) {
    Decision d;
    if (in.systemOnly) {
      d.render = true;
      d.flush = true;
      drewOverDriver = false;
      return d;
    }
    switch (in.owner) {
      case Owner::Lvgl:
        // The app's frames carry the layer. Once an app owns the glass the
        // driver's content is gone for good; nothing to put back.
        drewOverDriver = false;
        d.render = !in.suspended;
        d.flush = !in.suspended;
        break;
      case Owner::None:
        // Suspension is an app's: with no app owning the target, it does not
        // apply.
        if (in.visible) {
          d.render = true;
          d.flush = true;
          drewOverDriver = true;
        } else if (drewOverDriver) {
          d.restore = true;
          drewOverDriver = false;
        }
        break;
      case Owner::Other:
        // lgfx drew everything there is; it repaints its own frames.
        drewOverDriver = false;
        break;
    }
    return d;
  }
};

}  // namespace Resident

#endif  // RESIDENT_SYSTEM_LAYER_H
