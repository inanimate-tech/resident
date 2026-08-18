// Ported from m5stick-arc device/lib/drivers/src/LVGLDriver.cpp; reworked
// for ResidentLvglModule (see LVGLDriver.h for the provenance note).
#ifdef HAS_LVGL

#include "LVGLDriver.h"
#include <esp_heap_caps.h>

// LVGL draw buffer: ~1/10 screen, single buffer — the flush below is
// synchronous, so a second buffer buys no overlap; allocated in begin()
// from internal DMA-capable RAM (the SPI write path never touches PSRAM
// mid-transfer).
static constexpr int BUF_ROWS = 14;  // 240 * 14 * 2B = 6.7KB

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  // LVGL renders RGB565 little-endian; the panel wants big-endian.
  // Swap in place — scoped here so the sprite-based screen.*/lgfx path,
  // which manages its own byte order, is untouched.
  uint16_t* p = (uint16_t*)px_map;
  uint32_t n = (uint32_t)w * (uint32_t)h;
  for (uint32_t i = 0; i < n; i++) p[i] = __builtin_bswap16(p[i]);
  M5.Display.startWrite();
  M5.Display.setAddrWindow(area->x1, area->y1, w, h);
  M5.Display.writePixels((const uint16_t*)px_map, w * h);
  M5.Display.endWrite();
  lv_display_flush_ready(disp);
}

void LVGLDriver::begin()
{
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  size_t bufBytes = (size_t)M5.Display.width() * BUF_ROWS * sizeof(lv_color_t);
  void* buf = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  _disp = lv_display_create(M5.Display.width(), M5.Display.height());
  lv_display_set_buffers(_disp, buf, nullptr, bufBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(_disp, flush_cb);
  lv_display_set_dpi(_disp, 240);   // 240x135 on 25x13mm of glass
  // No theme baked here: the reference driver installed the arc C theme,
  // which is app-framework taste, not glue. Apps (or a framework) style the
  // display from Lua: lvgl.bind("main"):set_theme{...}.
}

void LVGLDriver::update()
{
  if (!_disp) return;
  // Loop task == Lua task: no locking. Animations advance at up to 200Hz,
  // decoupled from the sandbox's 10Hz on_tick; the period gate keeps a
  // multi-kHz idle loop from grinding LVGL's timer list continuously.
  lv_timer_handler_run_in_period(5);
}

void LVGLDriver::onAppReset()
{
  if (!_disp) return;
  // Wipe the outgoing app's tree. Safe with stale Lua handles — luavgl
  // invalidates them on C-side deletion (fork tests/appswap.lua). The panel
  // itself is shared with the sprite-backed screen.*/lgfx surface; last
  // writer wins on the glass.
  lv_obj_clean(lv_display_get_screen_active(_disp));
}

#endif // HAS_LVGL
