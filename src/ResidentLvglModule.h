// src/ResidentLvglModule.h — the `lvgl` Lua module: retained-mode UI via
// LVGL 9 + luavgl (arc R17, R19).
//
// OPT-IN: this header is not included by Resident.h. Include it from board
// code that also lists LVGL and the Inanimate luavgl fork in its lib_deps (see
// examples/m5stick-demo's `m5stick-lvgl` env):
//
//   #include <ResidentLvglModule.h>
//   Resident::LvglModule lvglModule;
//   // the board registers its panel once, library-agnostically:
//   Resident::RenderTargets::addPanel("main", &myPanel);
//   lvglModule.addDisplay("main", {.dpi = 240});
//   cfg.extensions = {..., &lvglModule};
//
// Since R19 the MODULE owns the LVGL runtime, not the board: lv_init, the
// millis tick source, the lv_display_t, its draw buffers, the flush callback
// (over the panel's blit), the lv_timer_handler pump and the app-reset tree
// wipe all live here. A board supplies only "the panel, addressable" — a
// PanelTarget with geometry and a raw blit. There is no glue driver left to
// write.
//
// Lua binds by name and gets luavgl's display-scoped handle (see the fork's
// docs/display-bind.md for the full handle surface):
//
//   local h = lvgl.bind("main")   -- errors on unknown names
//   h.Label { text = "hi" }       -- parents to the display's active screen
//   h:set_theme { screen = { bg_color = "#0b0b10" } }
//
// BIND IS THE CLAIM (R19). The lv_display_t is created on the FIRST bind
// (nothing is allocated on a board whose apps never use LVGL), the bind
// claims the target in the RenderTargets registry, and the whole active
// screen is invalidated so the takeover repaints every pixel the other
// library left behind. While this module does NOT own the target — because
// lgfx claimed it, or because an app reset released every claim — the flush
// callback returns without touching the panel (and the display's refresh
// timer is paused, so LVGL doesn't even render). That is how a wiped,
// unowned tree can no longer race a blank frame onto the glass behind the
// incoming app.
//
// Doctrine: everything LVGL — creation, mutation, timer pump, flush —
// happens on the loop task (the same task that runs all sandbox Lua), so no
// lv_lock is needed anywhere.
//
// On a build without LVGL/luavgl this header compiles to nothing (the same
// pattern as ResidentM5Mic.h), so cppcheck and native tests stay happy.
#ifndef RESIDENT_LVGL_MODULE_H
#define RESIDENT_LVGL_MODULE_H

#if __has_include(<lvgl.h>) && __has_include(<luavgl.h>)

#include <Arduino.h>
#include <cstring>
#include "ResidentExtension.h"
#include "ResidentLuaModule.h"
#include "ResidentRenderTargets.h"

// Lua headers carry C linkage but no C++ guards — wrap them. luavgl.h then
// re-includes them as no-ops (guards set) and provides its own extern "C"
// for its declarations; it must NOT sit inside this block or its transitive
// C++ includes break with "template with C linkage".
extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}
#include <lvgl.h>
#include <luavgl.h>

#if LV_COLOR_DEPTH != 16
#error "ResidentLvglModule requires LV_COLOR_DEPTH 16 — PanelTarget::blit is RGB565"
#endif

namespace Resident {

class LvglModule : public Extension {
public:
  static constexpr int MAX_DISPLAYS = 4;

  struct DisplayOptions {
    int32_t dpi = 0;           // 0 = leave LVGL's default
    int32_t bufferRows = 0;    // 0 = auto (panel height / 10, min 8 rows)
    void* buffer = nullptr;    // board-supplied draw buffer (e.g. DMA-capable
    size_t bufferBytes = 0;    // internal RAM); else allocated with lv_malloc
  };

  const char* name() const override { return "lvgl"; }

  // Firmware setup: declare that LVGL may drive a registered panel target.
  // The board must have registered the panel (RenderTargets::addPanel) —
  // geometry and the blit come from there. The lv_display_t is NOT created
  // here: it is created on the first `lvgl.bind(name)`.
  // (Two overloads rather than a default argument: GCC 8 — the arduino-esp32
  // toolchain compiler — cannot use a class's own default member initializers
  // in a default argument declared inside that class.)
  bool addDisplay(const char* displayName) {
    return addDisplay(displayName, DisplayOptions());
  }
  bool addDisplay(const char* displayName, const DisplayOptions& opts) {
    if (_count >= MAX_DISPLAYS || !displayName) return false;
    _slots[_count].name = displayName;
    _slots[_count].opts = opts;
    _count++;
    PanelTarget* p = RenderTargets::panel(displayName);
    RenderTargets::add(displayName, p ? p->width() : 0, p ? p->height() : 0,
                       nullptr, RenderTargets::MODULE_LVGL);
    return true;
  }

  // lv_init + the tick source. Displays come later (first bind), so a board
  // whose apps never touch LVGL pays only for the library's own init.
  void begin() override {
    if (!lv_is_initialized()) lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
  }

  // Pump LVGL. Animations advance at up to 200 Hz, decoupled from the
  // sandbox's 10 Hz on_tick; the period gate keeps a multi-kHz idle loop
  // from grinding LVGL's timer list continuously.
  void update() override {
    if (_displays == 0) return;
    lv_timer_handler_run_in_period(5);
  }

  // App reset: wipe the outgoing app's tree and release every claim. Safe
  // with stale Lua handles — luavgl invalidates them on C-side deletion
  // (fork tests/appswap.lua). The blank frame that the wipe invalidates is
  // never flushed: releasing first makes this module a non-owner, and the
  // flush gate drops it.
  void onAppReset() override {
    RenderTargets::release(RenderTargets::MODULE_LVGL);
    for (int i = 0; i < _count; i++) {
      if (!_slots[i].disp) continue;
      standDown(_slots[i]);
      lv_obj_clean(lv_display_get_screen_active(_slots[i].disp));
    }
  }

  void registerModule(LuaModule& m) override {
    m.method<LvglModule, &LvglModule::bind>("bind");
    // THE SHADOWING PROBLEM: the sandbox registers every extension as a
    // FRESH global table (Sandbox::initialize pass 2), so the global `lvgl`
    // would hide luavgl's own module table — its constants (lvgl.ALIGN,
    // lvgl.EVENT, ...), Font/Anim/Style/Timer, and the classic widget
    // constructors would all be unreachable. Fix: this table's missing keys
    // fall through (metatable __index) to the REAL luavgl module, resolved
    // LAZILY per lookup from the registry's _LOADED table — the module
    // lands there when luavgl_bind_display first runs luaL_requiref.
    //   * registry _LOADED, not package.loaded: the sandbox closes the
    //     package library to apps by default, but luaL_requiref maintains
    //     the registry table regardless — it exists with or without
    //     `package`, and apps cannot reach the registry to tamper with it.
    //   * lazy, not eager: requiring luavgl here would force a live display
    //     at module-registration time; the display is created on the first
    //     bind. Before that, fallthrough keys are nil — in practice apps
    //     always bind first.
    m.fallthrough(&LvglModule::l_moduleIndex);
  }

  // lvgl.bind(name) -> luavgl display-scoped handle (idempotent per display;
  // luavgl caches it). Creates the display on first use, then CLAIMS the
  // target: last bind wins, the other library stands down. Unknown name (or
  // a target with no registered panel) raises a Lua error — caught by the
  // app compile/init/tick pcall and reported like any other app error.
  int bind(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Slot* s = nullptr;
    for (int i = 0; i < _count; i++) {
      if (strcmp(_slots[i].name, name) == 0) { s = &_slots[i]; break; }
    }
    if (!s) return luaL_error(L, "lvgl.bind: no display named '%s'", name);
    if (!s->disp && !createDisplay(*s)) {
      return luaL_error(L, "lvgl.bind: '%s' has no panel to draw on", name);
    }
    RenderTargets::claim(name, RenderTargets::MODULE_LVGL);
    standUp(*s);
    return luavgl_bind_display(L, s->disp);
  }

private:
  struct Slot {
    const char* name = nullptr;
    lv_display_t* disp = nullptr;
    PanelTarget* panel = nullptr;
    DisplayOptions opts = {};
  };
  Slot _slots[MAX_DISPLAYS] = {};
  int _count = 0;
  int _displays = 0;

  // Create the lv_display_t over the board's panel: draw buffer, flush
  // callback, dpi. Returns false when no panel is registered for the name.
  bool createDisplay(Slot& s) {
    PanelTarget* p = RenderTargets::panel(s.name);
    if (!p) return false;
    int32_t w = p->width();
    int32_t h = p->height();
    if (w <= 0 || h <= 0) return false;

    int32_t rows = s.opts.bufferRows > 0 ? s.opts.bufferRows : h / 10;
    if (rows < 8) rows = 8;
    if (rows > h) rows = h;
    size_t bytes = (size_t)w * (size_t)rows * 2u;   // RGB565
    void* buf = s.opts.buffer;
    if (buf) {
      if (s.opts.bufferBytes < bytes) bytes = s.opts.bufferBytes;
    } else {
      buf = lv_malloc(bytes);
    }
    if (!buf || bytes < (size_t)w * 2u) return false;

    s.panel = p;
    s.disp = lv_display_create(w, h);
    if (!s.disp) return false;
    lv_display_set_user_data(s.disp, &s);
    lv_display_set_buffers(s.disp, buf, nullptr, bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s.disp, flush_cb);
    if (s.opts.dpi > 0) lv_display_set_dpi(s.disp, s.opts.dpi);
    _displays++;
    RenderTargets::add(s.name, w, h, nullptr, RenderTargets::MODULE_LVGL);
    return true;
  }

  // Standing down: pause the refresh timer so LVGL doesn't render frames
  // nobody will see. (The flush gate below is the correctness mechanism —
  // this is only the CPU saving. lv_display_set_default juggling is NOT the
  // mechanism: luavgl resolves parents at create time and the default
  // display is global state other code reads.)
  static void standDown(Slot& s) {
    lv_timer_t* t = lv_display_get_refr_timer(s.disp);
    if (t) lv_timer_pause(t);
  }

  // Taking over: resume rendering and invalidate everything — the other
  // library owned these pixels a moment ago, so nothing on the panel can be
  // assumed to match LVGL's idea of what is already drawn.
  static void standUp(Slot& s) {
    lv_timer_t* t = lv_display_get_refr_timer(s.disp);
    if (t) lv_timer_resume(t);
    lv_obj_t* screen = lv_display_get_screen_active(s.disp);
    if (screen) lv_obj_invalidate(screen);
  }

  // The one flush: gate on ownership, byte-swap, blit through the panel.
  static void flush_cb(lv_display_t* disp, const lv_area_t* area,
                       uint8_t* px_map) {
    Slot* s = (Slot*)lv_display_get_user_data(disp);
    if (!s || !s->panel ||
        !RenderTargets::isOwner(s->name, RenderTargets::MODULE_LVGL)) {
      lv_display_flush_ready(disp);   // stand down: LVGL is satisfied, the
      return;                         // glass is untouched
    }
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    // LVGL renders RGB565 little-endian; PanelTarget::blit takes the panel's
    // wire order (big-endian). Swap in place — the buffer is LVGL's own.
    uint16_t* px = (uint16_t*)px_map;
    uint32_t n = (uint32_t)w * (uint32_t)h;
    for (uint32_t i = 0; i < n; i++) px[i] = __builtin_bswap16(px[i]);
    s->panel->blit(area->x1, area->y1, w, h, px);
    lv_display_flush_ready(disp);
  }

  // __index for the extension's global table: (table, key) -> the real
  // luavgl module's value for key, or nil before the module first loads.
  // lua_gettable (not rawget) on the module: luavgl parks the widget
  // constructors behind the module's own "widgets" metatable.
  static int l_moduleIndex(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    if (!lua_istable(L, -1)) { lua_pushnil(L); return 1; }
    lua_getfield(L, -1, "lvgl");
    if (!lua_istable(L, -1)) { lua_pushnil(L); return 1; }
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
  }
};

} // namespace Resident

#endif // __has_include(<lvgl.h>) && __has_include(<luavgl.h>)
#endif // RESIDENT_LVGL_MODULE_H
