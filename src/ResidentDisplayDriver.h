// src/ResidentDisplayDriver.h
#ifndef RESIDENT_DISPLAY_DRIVER_H
#define RESIDENT_DISPLAY_DRIVER_H

#include <cstdint>
#include "ResidentDriver.h"
#include "ResidentRenderTargets.h"

struct lua_State;

namespace Resident {

// One physical glass a display driver owns: what an app draws on.
//
// Everything here is a fact the board states, readable at STATIC INIT: a
// board declares its drivers in a config function that commonly runs before
// any hardware exists, so a Screen never measures anything. Geometry is read
// from `target` when it is needed (PanelTarget::width/height), as before.
struct Screen {
  const char* name = nullptr;     // the name apps use: lvgl.bind("round"), screens.get("p3")
  PanelTarget* target = nullptr;  // geometry + the blit
  const char* shape = "rect";     // "rect" | "round"
  uint8_t depth = 16;             // 16 = colour, 1 = one-bit glass
  uint16_t dpi = 0;               // for a drawing library's sizing; 0 = its default
  uint16_t bufferRows = 0;        // lvgl draw-buffer rows; 0 = auto
  uint8_t group = 0;              // screens sharing a knob (one backlight rail) share a nonzero group
};

// A driver that owns one or more screens.
//
// List it in SandboxConfig::extensions and its screens exist: the sandbox
// registers them at initialize(), drawing libraries find them by name (no
// RenderTargets::addPanel, no per-screen addDisplay), and Lua reaches their
// settings through the `screens` module. One driver may own several screens —
// that is what the hardware usually is: seven e-paper panels on one bus, three
// key caps on one backlight rail.
//
// A SystemDisplay IS a DisplayDriver (with no screens until it says so):
// the panel that shows the connection text is very often also the one apps
// draw on — fan's TFT, oracle's round face, bar's tube, face's mouth — and a
// second Driver base would form a diamond and move the Extension subobject off
// offset 0, which the LuaModule trampoline requires. So a status-only display
// (an OLED status line, a status bar) simply declares no screens, and a
// dual-role one overrides screenCount()/screen().
//
// Settings are by key so a driver can offer what its glass has without
// Resident knowing it: "brightness" and "contrast" (0..1) are the standard
// names; a one-bit screen adds its own (dither, gamma, ...). A driver resets
// its settings to the board's defaults in onAppReset(), so every app starts
// from the same glass.
class DisplayDriver : public Driver {
public:
  DisplayDriver* asDisplayDriver() override { return this; }

  virtual int screenCount() const { return 0; }
  virtual Screen screen(int i) const { (void)i; return Screen(); }

  // screens.set(name, { key = value }): apply one key, the value at stack
  // index `idx`. Return false for a key this screen does not have — the Lua
  // call then raises, naming it. Raise yourself (luaL_error) for a bad value.
  virtual bool setScreen(int i, const char* key, lua_State* L, int idx) {
    (void)i; (void)key; (void)L; (void)idx;
    return false;
  }

  // screens.get(name): add this screen's current settings (and any status,
  // e.g. an e-paper panel's busy/pending) as fields of the table on top of
  // the stack. Leave the stack as found.
  virtual void getScreen(int i, lua_State* L) { (void)i; (void)L; }

  // screens.refresh(name): an e-paper panel's "update now". false when the
  // screen has no such thing.
  virtual bool refresh(int i) { (void)i; return false; }
};

} // namespace Resident

#endif // RESIDENT_DISPLAY_DRIVER_H
