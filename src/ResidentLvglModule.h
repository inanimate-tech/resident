// src/ResidentLvglModule.h — the `lvgl` Lua module: retained-mode UI via
// LVGL 9 + luavgl.
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
// The MODULE owns the LVGL runtime, not the board: lv_init, the
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
// THE SYSTEM LAYER. Firmware draws above whatever app is running through
// systemLayer(name): the display's lv_layer_sys, composed into the app's own
// frames, kept across app swaps (app reset deletes only what Lua made), and
// hidden from Lua. A display whose layer is in use is created then, not at an
// app's first bind, and the module keeps pumping LVGL with no app loaded.
// addSystemDisplay(name, panel) declares a display only the firmware draws on,
// which no app can bind or find. When the layer may render and flush is
// SystemLayerGate's decision (ResidentSystemLayer.h).
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
#include "ResidentDisplayDriver.h"
#include "ResidentSystemLayer.h"

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

// LVGL's "forget the pending invalid areas" (area == NULL), from its private
// refresh header.
extern "C" void lv_inv_area(lv_display_t* disp, const lv_area_t* area_p);

#if LV_COLOR_DEPTH != 16
#error "ResidentLvglModule requires LV_COLOR_DEPTH 16 — PanelTarget::blit is RGB565"
#endif

namespace Resident {

class LvglModule : public Extension {
public:
  // Eight: a device may be several panels rather than one screen, each
  // registered as its own display. A slot is a name, the options and a
  // pointer; nothing is allocated until that display's first bind.
  static constexpr int MAX_DISPLAYS = 8;

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
    // The module bit only: addDisplay runs from a board's config function,
    // which commonly runs during static init, and measuring a panel there is
    // a crash. Geometry is read when the display is created, on first bind.
    RenderTargets::declare(displayName, RenderTargets::MODULE_LVGL);
    return true;
  }

  // A display only the firmware draws on: a status glass no app may reach.
  // It is NOT registered in RenderTargets — `screens` does not list it,
  // `lvgl.bind` refuses its name, luavgl's display list skips it, and it is
  // never LVGL's default display. Its system layer is all it shows. Call from
  // setup() (the panel is measured when the display is created, at the first
  // systemLayer() call).
  bool addSystemDisplay(const char* displayName, PanelTarget* panel) {
    return addSystemDisplay(displayName, panel, DisplayOptions());
  }
  bool addSystemDisplay(const char* displayName, PanelTarget* panel,
                        const DisplayOptions& opts) {
    if (_count >= MAX_DISPLAYS || !displayName || !panel) return false;
    Slot& s = _slots[_count++];
    s.name = displayName;
    s.opts = opts;
    s.panel = panel;   // held here: a system-only display is not in RenderTargets
    s.systemOnly = true;
    return true;
  }

  // The system layer of a display, for firmware to draw on: lv_layer_sys of
  // that display, above every screen an app makes. Creates the display if no
  // app has bound it yet. Null when the name is unknown or has no panel.
  // Firmware objects made under it survive every app reset (luavgl_app_reset
  // deletes only objects Lua created). Loop task only, like all of LVGL.
  lv_obj_t* systemLayer(const char* displayName) {
    Slot* s = displayName ? slotFor(displayName) : nullptr;
    if (!s) return nullptr;
    if (!s->disp && !createDisplay(*s)) return nullptr;
    if (!s->system) {
      s->system = true;
      _systemLayers++;
    }
    return lv_display_get_layer_sys(s->disp);
  }

  // The board's own driver has just drawn on a glass the system layer is
  // drawn over — status text with no app, which reaches the glass without
  // LVGL — and so covered the layer. Ask for the layer's visible objects
  // again, so the next frame puts them back on top. A board calls this from
  // its driver after it draws; a no-op unless the layer is showing over a
  // glass nobody owns.
  void repaintSystemLayer(const char* displayName) {
    for (int i = 0; i < _count; i++) {
      Slot& s = _slots[i];
      if (!s.disp || !s.system || !s.drewOverDriver || strcmp(s.name, displayName) != 0) continue;
      invalidateLayer(s);
    }
  }

  // The module keeps LVGL running with no app loaded once any system layer is
  // in use: the layer must be able to draw at boot, between apps, and on a
  // system-only display that never has an app at all.
  bool runsWithoutApp() const override { return _systemLayers > 0; }

  // Fonts beyond LVGL's built-ins. Return nullptr for a family you don't
  // carry: luavgl then tries the next name in a comma list
  // (lvgl.Font("myface, montserrat", 24)). Set before the sandbox runs.
  using FontResolver = make_font_cb;   // const lv_font_t* (*)(const char* family, int size, int weight)
  void setFontResolver(FontResolver r) { _fontResolver = r; }

  // Widgets beyond luavgl's own: a board's C++ widget class, constructed from
  // Lua as parent:Name{...} like any other. The installer adds the
  // constructor to luavgl's "widgets" registry table (every object's method
  // lookup falls through to it) and creates the class's metatable with
  // luavgl_obj_newmetatable, so the widget's own `set` takes its own keys.
  // It runs on every bind, after luavgl is loaded (before that the widgets
  // table and the base metatable do not exist), so it must be idempotent —
  // luavgl_obj_newmetatable returns an existing metatable, and setting a
  // table field again costs nothing. Every bind rather than once per state
  // because the sandbox clears every global made after boot when the next
  // app loads, so a global an installer sets (a helper module) has to be
  // put back for each app. Up to MAX_WIDGETS; set before the sandbox runs.
  // An installer leaves the Lua stack as it found it.
  static constexpr int MAX_WIDGETS = 8;
  using WidgetInstaller = void (*)(lua_State* L);
  bool addWidget(WidgetInstaller install) {
    if (!install || _widgetCount >= MAX_WIDGETS) return false;
    _widgets[_widgetCount++] = install;
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
    for (int i = 0; i < _count; i++) {
      if (_slots[i].disp && _slots[i].system) gateSystemLayer(_slots[i]);
    }
    lv_timer_handler_run_in_period(5);
  }

  // The app stopped, was suspended, or resumed. A suspension is the overlay
  // arbiter taking a surface this app draws on (a dual-role surface: one that
  // is both a system display and an app screen), and the board suppresses its
  // own blit door for the duration. But suppression drops the PIXELS while LVGL
  // still renders and still gets its lv_display_flush_ready: the library goes
  // on believing those areas reached the glass, so when the claim lifts nothing
  // is dirty and the overlay's last frame sits there under a live app, with
  // only the widgets that happen to change painting over it.
  //
  // So stand the rendering down for the duration — the app is suspended, so
  // nothing it draws can change anyway — and on the way back stand up, which
  // invalidates the whole screen and repaints every pixel the overlay owned.
  // Only for targets this module still owns: before an app's first bind there
  // is nothing to repaint, and a target lgfx claimed is not ours to touch.
  void onAppRunning(bool running) override {
    _suspended = !running;
    for (int i = 0; i < _count; i++) {
      if (!_slots[i].disp || _slots[i].systemOnly) continue;
      if (!running) standDown(_slots[i]);
      else if (RenderTargets::isOwner(_slots[i].name, RenderTargets::MODULE_LVGL))
        standUp(_slots[i]);
    }
  }

  // App reset: every app starts on the screen a fresh boot would give it,
  // and every claim is released.
  //
  // Emptying the old screen (lv_obj_clean) is not enough: it deletes the
  // children but keeps the screen OBJECT, and with it whatever the outgoing
  // app set on the screen itself — `h.screen():set{ bg_color = ... }`, which
  // luavgl documents as the way to tweak one property, lands as a local
  // style, and a local style outranks any theme. On the clock, an app that
  // whitened its screen left every later app with a white ground behind
  // whatever it drew; arc re-applying its dark theme at bind could not
  // override it, and only a reboot cleared it. The display's THEME leaks the
  // same way: one Lua state lives as long as the device, and luavgl keeps a
  // `set_theme` installed in it until it is replaced.
  //
  // So: uninstall the theme Lua set (luavgl's own set_theme(nil), which
  // restores the theme underneath and keeps its bookkeeping straight), then
  // replace the screen with a new one — created under the restored theme —
  // and delete the old one, children and all. Safe with stale Lua handles:
  // luavgl invalidates them on C-side deletion (fork tests/appswap.lua), and
  // its bound constructors resolve the active screen at call time. The frame
  // this invalidates is never flushed: releasing first makes this module a
  // non-owner, and the flush gate drops it.
  //
  // Then luavgl_app_reset releases what is not on the active screen: the
  // app's timers and anims (which would otherwise go on running its
  // closures under the next app), its objects on the display layers and on
  // screens it never loaded, its theme styles and Styles. It runs after the
  // screens go, because the styles it releases must no longer be on a live
  // object.
  void onAppReset() override {
    RenderTargets::release(RenderTargets::MODULE_LVGL);
    _suspended = false;
    for (int i = 0; i < _count; i++) {
      if (!_slots[i].disp || _slots[i].systemOnly) continue;
      standDown(_slots[i]);
      uninstallLuaTheme(_slots[i].disp);
      replaceScreen(_slots[i].disp);
    }
    if (_lua && _displays > 0) luavgl_app_reset(_lua);
  }

  void registerModule(LuaModule& m) override {
    _lua = m.state();
    m.method<LvglModule, &LvglModule::bind>("bind");
    // A body's own fonts: luavgl resolves lvgl.Font(name, size, weight)
    // against its compiled-in built-ins first and hands everything else —
    // including a built-in family compiled out of lv_conf.h — to the
    // extension installed here. Installed once, when the sandbox sets up
    // its state (registerModule runs then), so set it before setup().
    if (_fontResolver) luavgl_set_font_extension(m.state(), _fontResolver, nullptr);
    pushSymbols(m.state());
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
    Slot* s = slotFor(name);
    if (!s || s->systemOnly) return luaL_error(L, "lvgl.bind: no display named '%s'", name);
    if (!s->disp && !createDisplay(*s)) {
      return luaL_error(L, "lvgl.bind: '%s' has no panel to draw on", name);
    }
    RenderTargets::claim(name, RenderTargets::MODULE_LVGL);
    standUp(*s);
    const int n = luavgl_bind_display(L, s->disp);
    hideSystemFromLua(L);
    for (int i = 0; i < _widgetCount; i++) _widgets[i](L);
    return n;
  }

private:
  // The slot for a display name: one the board declared with addDisplay, or
  // — for a display driver's screen (ResidentDisplayDriver.h) — one made on
  // first bind from the screen's own facts, so a board lists the driver and
  // nothing else.
  struct Slot;
  Slot* slotFor(const char* name) {
    for (int i = 0; i < _count; i++) {
      if (strcmp(_slots[i].name, name) == 0) return &_slots[i];
    }
    const int i = RenderTargets::indexOf(name);
    if (i < 0 || _count >= MAX_DISPLAYS) return nullptr;
    const RenderTargets::Entry& e = RenderTargets::entry(i);
    if (!e.panel || !e.driver) return nullptr;
    Slot& s = _slots[_count++];
    s.name = e.name;
    s.opts = DisplayOptions();
    s.opts.dpi = e.dpi;
    s.opts.bufferRows = e.bufferRows;
    RenderTargets::declare(e.name, RenderTargets::MODULE_LVGL);
    return &s;
  }

  struct Slot {
    const char* name = nullptr;
    lv_display_t* disp = nullptr;
    PanelTarget* panel = nullptr;
    DisplayOptions opts = {};
    bool systemOnly = false;
    bool system = false;              // systemLayer() has been asked for
    bool drewOverDriver = false;      // SystemLayerGate's memory
    bool flush = false;               // the gate's flush decision for the layer
    bool direct = false;              // LVGL draws into the panel's own frame
  };
  Slot _slots[MAX_DISPLAYS] = {};
  int _count = 0;
  int _displays = 0;
  int _systemLayers = 0;
  bool _suspended = false;
  bool _luaGuarded = false;
  lua_State* _lua = nullptr;   // the sandbox's one Lua state (registerModule)
  FontResolver _fontResolver = nullptr;
  WidgetInstaller _widgets[MAX_WIDGETS] = {};
  int _widgetCount = 0;

  // lvgl.SYMBOL.<NAME>: LVGL's symbol strings (lv_symbol_def.h) as a Lua
  // table, so an app can write lvgl.SYMBOL.PLAY .. " start". The glyphs
  // behind them exist only in a font that carries LVGL's icon range.
  static void pushSymbols(lua_State* L) {
    static const struct { const char* name; const char* utf8; } kSymbols[] = {
      {"BULLET", LV_SYMBOL_BULLET}, {"AUDIO", LV_SYMBOL_AUDIO}, {"VIDEO", LV_SYMBOL_VIDEO},
      {"LIST", LV_SYMBOL_LIST}, {"OK", LV_SYMBOL_OK}, {"CLOSE", LV_SYMBOL_CLOSE},
      {"POWER", LV_SYMBOL_POWER}, {"SETTINGS", LV_SYMBOL_SETTINGS}, {"HOME", LV_SYMBOL_HOME},
      {"DOWNLOAD", LV_SYMBOL_DOWNLOAD}, {"DRIVE", LV_SYMBOL_DRIVE}, {"REFRESH", LV_SYMBOL_REFRESH},
      {"MUTE", LV_SYMBOL_MUTE}, {"VOLUME_MID", LV_SYMBOL_VOLUME_MID}, {"VOLUME_MAX", LV_SYMBOL_VOLUME_MAX},
      {"IMAGE", LV_SYMBOL_IMAGE}, {"TINT", LV_SYMBOL_TINT}, {"PREV", LV_SYMBOL_PREV},
      {"PLAY", LV_SYMBOL_PLAY}, {"PAUSE", LV_SYMBOL_PAUSE}, {"STOP", LV_SYMBOL_STOP},
      {"NEXT", LV_SYMBOL_NEXT}, {"EJECT", LV_SYMBOL_EJECT}, {"LEFT", LV_SYMBOL_LEFT},
      {"RIGHT", LV_SYMBOL_RIGHT}, {"PLUS", LV_SYMBOL_PLUS}, {"MINUS", LV_SYMBOL_MINUS},
      {"EYE_OPEN", LV_SYMBOL_EYE_OPEN}, {"EYE_CLOSE", LV_SYMBOL_EYE_CLOSE}, {"WARNING", LV_SYMBOL_WARNING},
      {"SHUFFLE", LV_SYMBOL_SHUFFLE}, {"UP", LV_SYMBOL_UP}, {"DOWN", LV_SYMBOL_DOWN},
      {"LOOP", LV_SYMBOL_LOOP}, {"DIRECTORY", LV_SYMBOL_DIRECTORY}, {"UPLOAD", LV_SYMBOL_UPLOAD},
      {"CALL", LV_SYMBOL_CALL}, {"CUT", LV_SYMBOL_CUT}, {"COPY", LV_SYMBOL_COPY},
      {"SAVE", LV_SYMBOL_SAVE}, {"BARS", LV_SYMBOL_BARS}, {"ENVELOPE", LV_SYMBOL_ENVELOPE},
      {"CHARGE", LV_SYMBOL_CHARGE}, {"PASTE", LV_SYMBOL_PASTE}, {"BELL", LV_SYMBOL_BELL},
      {"KEYBOARD", LV_SYMBOL_KEYBOARD}, {"GPS", LV_SYMBOL_GPS}, {"FILE", LV_SYMBOL_FILE},
      {"WIFI", LV_SYMBOL_WIFI}, {"BATTERY_FULL", LV_SYMBOL_BATTERY_FULL}, {"BATTERY_3", LV_SYMBOL_BATTERY_3},
      {"BATTERY_2", LV_SYMBOL_BATTERY_2}, {"BATTERY_1", LV_SYMBOL_BATTERY_1}, {"BATTERY_EMPTY", LV_SYMBOL_BATTERY_EMPTY},
      {"USB", LV_SYMBOL_USB}, {"BLUETOOTH", LV_SYMBOL_BLUETOOTH}, {"TRASH", LV_SYMBOL_TRASH},
      {"EDIT", LV_SYMBOL_EDIT}, {"BACKSPACE", LV_SYMBOL_BACKSPACE}, {"SD_CARD", LV_SYMBOL_SD_CARD},
      {"NEW_LINE", LV_SYMBOL_NEW_LINE},
    };
    lua_createtable(L, 0, (int)(sizeof(kSymbols) / sizeof(kSymbols[0])));
    for (const auto& s : kSymbols) {
      lua_pushstring(L, s.utf8);
      lua_setfield(L, -2, s.name);
    }
    lua_setfield(L, -2, "SYMBOL");   // the module table is at -2 during registerModule
  }

  // Create the lv_display_t over the board's panel: draw buffer, flush
  // callback, dpi. Returns false when no panel is registered for the name.
  bool createDisplay(Slot& s) {
    PanelTarget* p = s.systemOnly ? s.panel : RenderTargets::panel(s.name);
    if (!p) return false;
    int32_t w = p->width();
    int32_t h = p->height();
    if (w <= 0 || h <= 0) return false;

    // A panel that keeps a whole frame lends it (PanelTarget::frame): LVGL
    // then draws each changed area straight into it, in one pass, and the
    // panel is told which pixels changed. Otherwise a partial draw buffer,
    // whose bands are swapped and handed to blit().
    PanelTarget::Frame frame;
    const bool direct = !s.opts.buffer && p->frame(frame) && frame.px &&
                        frame.stride == w;
    void* buf = nullptr;
    size_t bytes = 0;
    if (direct) {
      buf = frame.px;
      bytes = (size_t)w * (size_t)h * 2u;
    } else {
      int32_t rows = s.opts.bufferRows > 0 ? s.opts.bufferRows : h / 10;
      if (rows < 8) rows = 8;
      if (rows > h) rows = h;
      bytes = (size_t)w * (size_t)rows * 2u;   // RGB565
      buf = s.opts.buffer;
      if (buf) {
        if (s.opts.bufferBytes < bytes) bytes = s.opts.bufferBytes;
      } else {
        buf = lv_malloc(bytes);
      }
      if (!buf || bytes < (size_t)w * 2u) return false;
    }

    s.panel = p;
    s.direct = direct;
    // A system-only display must never become LVGL's default: luavgl's
    // unbound constructors and lvgl.disp.get_default() resolve through it.
    lv_display_t* prevDefault = lv_display_get_default();
    s.disp = lv_display_create(w, h);
    if (s.systemOnly) lv_display_set_default(prevDefault);
    if (!s.disp) return false;
    lv_display_set_user_data(s.disp, &s);
    lv_display_set_buffers(s.disp, buf, nullptr, bytes,
                           direct ? LV_DISPLAY_RENDER_MODE_DIRECT
                                  : LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s.disp, flush_cb);
    if (s.opts.dpi > 0) lv_display_set_dpi(s.disp, s.opts.dpi);
    _displays++;
    if (!s.systemOnly) RenderTargets::add(s.name, w, h, nullptr, RenderTargets::MODULE_LVGL);
    return true;
  }

  // Undo a Lua `h:set_theme{...}` on this display, through luavgl's own
  // set_theme(nil) so its per-display record (registry "luavgl.themes")
  // agrees that nothing is installed — the next app's set_theme then installs
  // afresh. The bound handle is luavgl's cache (registry "luavgl.bound",
  // keyed by the display); no handle means no app ever bound it, so nothing
  // to undo.
  void uninstallLuaTheme(lv_display_t* disp) {
    if (!_lua) return;
    lua_State* L = _lua;
    const int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "luavgl.bound");
    if (lua_istable(L, -1)) {
      lua_pushlightuserdata(L, disp);
      lua_rawget(L, -2);
      if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "set_theme");
        if (lua_isfunction(L, -1)) {
          lua_pushnil(L);
          if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            Serial.printf("[lvgl] theme reset failed: %s\n", lua_tostring(L, -1));
          }
        }
      }
    }
    lua_settop(L, top);
  }

  // A new, empty screen in place of the active one, which LVGL deletes with
  // everything on it (an immediate load with auto_del). lv_obj_create(NULL)
  // makes a screen on the DEFAULT display, so the default is pointed here
  // for that one call and restored — the global is only borrowed, on the
  // loop task, with nothing in between.
  static void replaceScreen(lv_display_t* disp) {
    lv_display_t* prev = lv_display_get_default();
    lv_display_set_default(disp);
    lv_obj_t* fresh = lv_obj_create(nullptr);
    lv_display_set_default(prev);
    if (!fresh) {                       // out of LVGL memory: at least empty it
      lv_obj_clean(lv_display_get_screen_active(disp));
      return;
    }
    lv_screen_load_anim(fresh, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
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

  // The layer starts drawing over a glass nobody owns. Whatever LVGL has
  // pending — its own empty screen at creation or after an app reset, the
  // areas objects passed through while the layer was hidden — is not on the
  // glass and must not reach it: render it now and drop it at the gate, then
  // ask for the layer's visible objects alone. From here, while nobody owns
  // the glass, nothing but the layer changes, so nothing else is redrawn.
  //
  // Into the panel's own frame there is nothing to render and drop: a
  // rendered area would land in the frame, which must go on holding what the
  // glass shows. The pending areas are forgotten instead.
  static void settleOverDriver(Slot& s) {
    s.flush = false;
    if (s.direct) lv_inv_area(s.disp, nullptr);
    else lv_refr_now(s.disp);
    invalidateLayer(s);
  }

  // Ask LVGL to redraw the system layer's visible objects.
  static void invalidateLayer(Slot& s) {
    lv_obj_t* layer = lv_display_get_layer_sys(s.disp);
    const uint32_t n = layer ? lv_obj_get_child_count(layer) : 0;
    for (uint32_t i = 0; i < n; i++) {
      lv_obj_t* c = lv_obj_get_child(layer, (int32_t)i);
      if (!lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) lv_obj_invalidate(c);
    }
  }

  // Whether anything on a display's system layer is showing.
  static bool layerVisible(lv_display_t* disp) {
    lv_obj_t* layer = lv_display_get_layer_sys(disp);
    const uint32_t n = layer ? lv_obj_get_child_count(layer) : 0;
    for (uint32_t i = 0; i < n; i++) {
      lv_obj_t* c = lv_obj_get_child(layer, (int32_t)i);
      if (!lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN) &&
          lv_obj_get_style_opa(c, LV_PART_MAIN) > LV_OPA_TRANSP) {
        return true;
      }
    }
    return false;
  }

  // Apply SystemLayerGate to one display, once per update(): the refresh
  // timer, the flush decision, and the board's repaint when the layer empties
  // over a target nobody owns.
  void gateSystemLayer(Slot& s) {
    SystemLayerGate::Inputs in;
    const uint8_t owner = RenderTargets::owner(s.name);
    in.owner = owner == 0 ? SystemLayerGate::Owner::None
             : owner == RenderTargets::MODULE_LVGL ? SystemLayerGate::Owner::Lvgl
             : SystemLayerGate::Owner::Other;
    in.systemOnly = s.systemOnly;
    in.visible = layerVisible(s.disp);
    in.suspended = _suspended;
    const bool wasOverDriver = s.drewOverDriver;
    const SystemLayerGate::Decision d = SystemLayerGate::decide(in, s.drewOverDriver);
    if (!wasOverDriver && s.drewOverDriver) settleOverDriver(s);
    s.flush = d.flush;
    // While an app owns the target its own stand-down/stand-up (bind, reset,
    // suspension) runs the refresh timer; the gate drives it otherwise. Set
    // every pass, not on change: app reset pauses it behind the gate's back.
    if (in.owner != SystemLayerGate::Owner::Lvgl) {
      lv_timer_t* t = lv_display_get_refr_timer(s.disp);
      if (t) {
        if (d.render) lv_timer_resume(t);
        else lv_timer_pause(t);
      }
    }
    if (d.restore) {
      const int i = RenderTargets::indexOf(s.name);
      DisplayDriver* drv = i < 0 ? nullptr : RenderTargets::entry(i).driver;
      if (drv) drv->restoreContent();
    }
  }

  // Keep the system layer and system-only displays out of Lua's reach, once
  // per Lua state (the first bind loads luavgl). luavgl's display handles
  // carry get_layer_sys, and its display list (lvgl.disp.get_next, a handle's
  // get_next) would hand an app a system-only display. Both are filtered here
  // in Resident rather than in the luavgl fork.
  void hideSystemFromLua(lua_State* L) {
    if (_luaGuarded) return;
    _luaGuarded = true;
    const int top = lua_gettop(L);
    // Handles: wrap the metatable's __index (luavgl's read-only method table).
    if (luaL_getmetatable(L, "lv_disp") == LUA_TTABLE) {
      lua_getfield(L, -1, "__index");
      lua_pushlightuserdata(L, this);
      lua_pushcclosure(L, &LvglModule::l_dispIndex, 2);
      lua_setfield(L, -2, "__index");
    }
    lua_settop(L, top);
    // The library table: lvgl.disp becomes an empty table that reads luavgl's
    // through the same filter, so whatever luavgl adds there stays reachable.
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    lua_getfield(L, -1, "lvgl");
    if (lua_istable(L, -1)) {
      lua_newtable(L);                       // the proxy
      lua_newtable(L);                       // its metatable
      lua_getfield(L, -3, "disp");
      lua_pushlightuserdata(L, this);
      lua_pushcclosure(L, &LvglModule::l_dispIndex, 2);
      lua_setfield(L, -2, "__index");
      lua_setmetatable(L, -2);
      lua_setfield(L, -2, "disp");
    }
    lua_settop(L, top);
  }

  bool isSystemOnly(const lv_display_t* d) const {
    for (int i = 0; i < _count; i++) {
      if (_slots[i].disp == d) return _slots[i].systemOnly;
    }
    return false;
  }

  // luavgl's lv_disp userdata holds the display pointer as its first field.
  static lv_display_t* dispOf(lua_State* L, int idx) {
    void* ud = luaL_testudata(L, idx, "lv_disp");
    return ud ? *static_cast<lv_display_t**>(ud) : nullptr;
  }

  // get_next(prev) with system-only displays skipped. Upvalues: luavgl's own
  // get_next, this module.
  static int l_dispGetNext(lua_State* L) {
    auto* self = static_cast<LvglModule*>(lua_touserdata(L, lua_upvalueindex(2)));
    lua_settop(L, 1);
    for (int guard = 0; guard <= MAX_DISPLAYS + 1; guard++) {
      lua_pushvalue(L, lua_upvalueindex(1));
      lua_pushvalue(L, -2);
      lua_call(L, 1, 1);
      lv_display_t* d = dispOf(L, -1);
      if (!d || !self->isSystemOnly(d)) return 1;
      lua_replace(L, -2);   // skip it: ask for the one after
    }
    lua_pushnil(L);
    return 1;
  }

  // __index for lv_disp handles and for the lvgl.disp library: luavgl's own,
  // minus get_layer_sys, with get_next filtered. Upvalues: luavgl's table,
  // this module.
  static int l_dispIndex(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (key && strcmp(key, "get_layer_sys") == 0) {
      lua_pushnil(L);
      return 1;
    }
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (key && strcmp(key, "get_next") == 0 && lua_isfunction(L, -1)) {
      lua_pushvalue(L, lua_upvalueindex(2));
      lua_pushcclosure(L, &LvglModule::l_dispGetNext, 2);
    }
    return 1;
  }

  // The one flush: gate on ownership, byte-swap, blit through the panel.
  static void flush_cb(lv_display_t* disp, const lv_area_t* area,
                       uint8_t* px_map) {
    Slot* s = (Slot*)lv_display_get_user_data(disp);
    const bool ours = s && (s->system
        ? s->flush
        : RenderTargets::isOwner(s->name, RenderTargets::MODULE_LVGL));
    if (!s || !s->panel || !ours) {
      lv_display_flush_ready(disp);   // stand down: LVGL is satisfied, the
      return;                         // glass is untouched
    }
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    if (s->direct) {
      // The area is already in the panel's frame.
      s->panel->presentFrame(area->x1, area->y1, w, h);
    } else {
      // LVGL renders RGB565 little-endian; PanelTarget::blit takes the panel's
      // wire order (big-endian). Swap in place — the buffer is LVGL's own.
      uint16_t* px = (uint16_t*)px_map;
      uint32_t n = (uint32_t)w * (uint32_t)h;
      for (uint32_t i = 0; i < n; i++) px[i] = __builtin_bswap16(px[i]);
      s->panel->blit(area->x1, area->y1, w, h, px);
    }
    const bool last = lv_display_flush_is_last(disp);
    if (last) s->panel->frameDone();
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
