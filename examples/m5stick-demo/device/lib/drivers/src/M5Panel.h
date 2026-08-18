// M5Panel — "the panel, addressable".
//
// The whole board-side render target: geometry and a raw RGB565 blit over
// M5GFX. Every pixel that reaches the glass goes through here — the
// status/app sprite (DisplayDriver), the lgfx module's frame push, and
// LVGL's partial flushes alike. Registered once by name:
//
//   Resident::RenderTargets::addPanel("main", &m5Panel);
//
// The graphics modules build their own machinery over it when Lua binds
// (sprite for lgfx, lv_display_t + draw buffers for lvgl), so there is no
// library-specific glue driver on the board any more.
//
// Byte order: PanelTarget::blit takes RGB565 big-endian — the SPI wire
// order, which is also LovyanGFX's internal 16-bit sprite format, and what
// writePixels() consumes when the panel's swapBytes flag is off (default).
#ifndef M5_PANEL_H
#define M5_PANEL_H

#include <M5Unified.h>
#include <ResidentRenderTargets.h>

class M5Panel : public Resident::PanelTarget {
public:
  int32_t width() const override { return M5.Display.width(); }
  int32_t height() const override { return M5.Display.height(); }

  void blit(int32_t x, int32_t y, int32_t w, int32_t h,
            const uint16_t* px) override {
    if (!px || w <= 0 || h <= 0) return;
    M5.Display.startWrite();
    M5.Display.setAddrWindow(x, y, w, h);
    M5.Display.writePixels(px, (int32_t)w * h);
    M5.Display.endWrite();
  }
};

#endif // M5_PANEL_H
