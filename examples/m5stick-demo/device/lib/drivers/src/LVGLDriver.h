// LVGLDriver — display/flush/tick glue for LVGL on the stick.
//
// Provenance: ported from the m5stick-arc reference project
// (device/lib/drivers/src/LVGLDriver.{h,cpp}), reworked for
// ResidentLvglModule: the driver no longer owns the Lua surface (no
// lvgl.open(), no luavgl embedding here — apps reach LVGL through
// Resident's LvglModule and lvgl.bind("main")), and it bakes no theme
// (the reference's arc C theme was app-framework taste; themes now come
// from Lua via the bound handle's h:set_theme{...}).
//
// What remains is exactly the glue Resident cannot know: lv_init, the
// lv_display_t over M5GFX with a DMA draw buffer and byte-swap flush,
// the millis tick source, the lv_timer_handler pump, and the app-reset
// wipe of the LVGL tree.
//
// Doctrine (unchanged from the reference): everything LVGL — creation,
// mutation, timer pump, flush — happens on the loop task (the same task
// that runs all sandbox Lua), so no lv_lock is needed anywhere.
#ifndef LVGL_DRIVER_H
#define LVGL_DRIVER_H

#ifdef HAS_LVGL

#include <ResidentExtension.h>
#include <M5Unified.h>
#include <lvgl.h>

// An Extension, not a Driver: it exposes no Lua module and fires no events.
// Its name() only labels the (empty) global the sandbox registers; the Lua
// surface lives on the LvglModule's `lvgl` global.
class LVGLDriver : public Resident::Extension {
public:
  const char* name() const override { return "lvgl_glue"; }

  // lv_init + display creation. Call early (Extension::beginExtension) so
  // main.cpp can register the display: lvglModule.addDisplay("main",
  // lvglDriver.display()) — beginExtension is idempotent, the sandbox's own
  // begin pass then no-ops.
  void begin() override;

  void update() override;      // pump lv_timer_handler at loop rate
  void onAppReset() override;  // delete the app's LVGL tree

  // The display to register with Resident::LvglModule. Null before begin().
  lv_display_t* display() const { return _disp; }

private:
  lv_display_t* _disp = nullptr;
};

#endif // HAS_LVGL
#endif // LVGL_DRIVER_H
