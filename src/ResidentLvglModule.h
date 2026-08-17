// src/ResidentLvglModule.h — the `lvgl` Lua module: retained-mode UI via
// LVGL + luavgl (arc R17).
//
// OPT-IN: this header is not included by Resident.h. Include it from board
// code that also lists LVGL and the luavgl arc fork in its lib_deps (see
// examples/m5stick-demo's `m5stick-lvgl` env):
//
//   #include <ResidentLvglModule.h>
//   Resident::LvglModule lvglModule;
//   // after the display driver has created its lv_display_t:
//   lvglModule.addDisplay("main", disp);          // shape "rect" by default
//   cfg.extensions = {..., &lvglModule};
//
// Lua binds by name and gets luavgl's display-scoped handle (see the fork's
// docs/display-bind.md for the full handle surface):
//
//   local h = lvgl.bind("main")   -- errors on unknown names
//   h.Label { text = "hi" }       -- parents to the display's active screen
//   h:set_theme { screen = { bg_color = "#0b0b10" } }
//
// The firmware side of LVGL — lv_init, display/flush/tick glue, the
// lv_timer_handler pump — stays the board's job (a small Extension; see
// examples/m5stick-demo/device/lib/drivers/LVGLDriver.*). This module owns
// only the name -> lv_display_t table and the Lua entry point.
//
// Per-target ownership is deliberately NOT duplicated here: luavgl caches
// one handle per display and widget creation resolves the active screen at
// call time, so "last bind wins" at the display level is luavgl's business.
// Wiping between owners is the embedder's reset path (lv_obj_clean from the
// glue driver's onAppReset, or h.clean() from Lua).
//
// On a build without LVGL/luavgl this header compiles to nothing (the same
// pattern as ResidentM5Mic.h), so cppcheck and native tests stay happy.
#ifndef RESIDENT_LVGL_MODULE_H
#define RESIDENT_LVGL_MODULE_H

#if __has_include(<lvgl.h>) && __has_include(<luavgl.h>)

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

namespace Resident {

class LvglModule : public Extension {
public:
  static constexpr int MAX_DISPLAYS = 4;

  const char* name() const override { return "lvgl"; }

  // Firmware setup: register an lv_display_t under a bind name. The display
  // must already exist (drivers that create it in begin() can be begun early
  // via Extension::beginExtension — see the example's main.cpp). Also
  // declares the surface in the RenderTargets registry for the device hello.
  bool addDisplay(const char* displayName, lv_display_t* disp,
                  const char* shape = "rect") {
    if (_count >= MAX_DISPLAYS || !displayName || !disp) return false;
    _slots[_count].name = displayName;
    _slots[_count].disp = disp;
    _count++;
    RenderTargets::add(displayName,
                       lv_display_get_horizontal_resolution(disp),
                       lv_display_get_vertical_resolution(disp),
                       shape, RenderTargets::MODULE_LVGL);
    return true;
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
    //   * lazy, not eager: requiring luavgl here would force lv_init and a
    //     live display at module-registration time; boards that create the
    //     display later (or lazily) would crash in luaopen_lvgl's root
    //     object creation. Before the first bind, fallthrough keys are nil
    //     — in practice apps always bind first.
    m.fallthrough(&LvglModule::l_moduleIndex);
  }

  // lvgl.bind(name) -> luavgl display-scoped handle (idempotent per display;
  // luavgl caches it). Unknown name raises a Lua error — caught by the app
  // compile/init/tick pcall and reported like any other app error.
  int bind(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lv_display_t* d = nullptr;
    for (int i = 0; i < _count; i++) {
      if (strcmp(_slots[i].name, name) == 0) { d = _slots[i].disp; break; }
    }
    if (!d) return luaL_error(L, "lvgl.bind: no display named '%s'", name);
    return luavgl_bind_display(L, d);
  }

private:
  struct Slot {
    const char* name = nullptr;
    lv_display_t* disp = nullptr;
  };
  Slot _slots[MAX_DISPLAYS] = {};
  int _count = 0;

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
