// src/ResidentLgfxModule.h — the `lgfx` Lua module: resident's honest
// drawing library (arc A7).
//
// Exposes idiomatic LovyanGFX drawing to Lua so models write against a
// library they know from training data instead of invented per-device
// verbs:
//
//   local g = lgfx.bind("main")
//   g:fillScreen(0x000000)
//   g:fillCircle(80, 60, 20, 0xFF5533)
//   g:setTextColor(0xFFFFFF); g:setCursor(4, 4); g:print("hi")
//   g:flip()
//
// Layering (native testability without compiling LovyanGFX host-side):
// - LgfxTarget: a minimal abstract interface carrying the core drawing
//   set. Colors are 24-bit 0xRRGGBB everywhere; depth conversion is the
//   target's job.
// - LgfxLovyanTarget<TGfx>: a header-only, duck-typed template adapter.
//   Instantiate with anything shaped like LovyanGFX's drawing API — an
//   lgfx::LGFX_Device, an LGFX_Sprite/M5Canvas, M5GFX — or a test fake.
//   No LovyanGFX include here; the firmware's own include provides TGfx.
//   LovyanGFX interprets uint32_t color arguments as RGB888, so colors
//   pass straight through and the panel/sprite converts to its own depth.
// - LgfxModule: the optional Extension. Firmware registers displays at
//   setup (addDisplay) and lists the module in SandboxConfig::extensions;
//   Lua binds by name.
//
// Present-to-glass: g:flip() calls the target's flip(). For sprite-backed
// devices, construct the adapter with a presenter callback that pushes the
// sprite (e.g. the driver's repaint()/pushSprite path). For direct-to-panel
// targets, omit the presenter — flip() is then a no-op and drawing is live.
//
// Deliberately small this pass: default font + setTextSize multiplier only,
// no font selection, no images, no Lua-created sprites.
#ifndef RESIDENT_LGFX_MODULE_H
#define RESIDENT_LGFX_MODULE_H

#include <Arduino.h>
#include <cstring>
#include <functional>
#include "ResidentExtension.h"
#include "ResidentLuaModule.h"
#include "ResidentRenderTargets.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

namespace Resident {

// ── The minimal drawing interface (colors: 24-bit 0xRRGGBB) ──────────────
class LgfxTarget {
public:
  virtual ~LgfxTarget() = default;

  virtual void fillScreen(uint32_t color) = 0;
  virtual void drawPixel(int32_t x, int32_t y, uint32_t color) = 0;
  virtual void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) = 0;
  virtual void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) = 0;
  virtual void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) = 0;
  virtual void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) = 0;
  virtual void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color) = 0;
  virtual void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color) = 0;
  virtual void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t color) = 0;
  virtual void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint32_t color) = 0;
  virtual void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint32_t color) = 0;

  virtual void setTextColor(uint32_t fg, uint32_t bg, bool hasBg) = 0;
  virtual void setTextSize(float size) = 0;
  virtual void setTextDatum(uint8_t datum) = 0;
  virtual void setCursor(int32_t x, int32_t y) = 0;
  virtual void print(const char* text) = 0;
  virtual void drawString(const char* text, int32_t x, int32_t y) = 0;

  virtual int32_t width() = 0;
  virtual int32_t height() = 0;

  // Present the frame to the glass. Default: drawing is live, nothing to do.
  virtual void flip() {}
};

// ── Duck-typed LovyanGFX adapter (also instantiable with a test fake) ────
template <class TGfx>
class LgfxLovyanTarget : public LgfxTarget {
public:
  using Presenter = std::function<void()>;

  explicit LgfxLovyanTarget(TGfx* gfx, Presenter present = nullptr)
      : _gfx(gfx), _present(std::move(present)) {}

  void fillScreen(uint32_t c) override { _gfx->fillScreen(c); }
  void drawPixel(int32_t x, int32_t y, uint32_t c) override { _gfx->drawPixel(x, y, c); }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) override {
    _gfx->drawLine(x0, y0, x1, y1, c);
  }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override {
    _gfx->drawRect(x, y, w, h, c);
  }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override {
    _gfx->fillRect(x, y, w, h, c);
  }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) override {
    _gfx->drawRoundRect(x, y, w, h, r, c);
  }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) override {
    _gfx->fillRoundRect(x, y, w, h, r, c);
  }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) override {
    _gfx->drawCircle(x, y, r, c);
  }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) override {
    _gfx->fillCircle(x, y, r, c);
  }
  void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint32_t c) override {
    _gfx->drawTriangle(x0, y0, x1, y1, x2, y2, c);
  }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint32_t c) override {
    _gfx->fillTriangle(x0, y0, x1, y1, x2, y2, c);
  }

  void setTextColor(uint32_t fg, uint32_t bg, bool hasBg) override {
    if (hasBg) _gfx->setTextColor(fg, bg);
    else       _gfx->setTextColor(fg);
  }
  void setTextSize(float size) override { _gfx->setTextSize(size); }
  void setTextDatum(uint8_t datum) override { _gfx->setTextDatum(datum); }
  void setCursor(int32_t x, int32_t y) override { _gfx->setCursor(x, y); }
  void print(const char* text) override { _gfx->print(text); }
  void drawString(const char* text, int32_t x, int32_t y) override {
    _gfx->drawString(text, x, y);
  }

  int32_t width() override { return _gfx->width(); }
  int32_t height() override { return _gfx->height(); }

  void flip() override { if (_present) _present(); }

private:
  TGfx* _gfx;
  Presenter _present;
};

// ── The Lua module ────────────────────────────────────────────────────────
class LgfxModule : public Extension {
public:
  static constexpr int MAX_DISPLAYS = 4;

  const char* name() const override { return "lgfx"; }

  // Firmware setup: register a display under a bind name. Call before
  // Sandbox::setup(). Returns false when the table is full. Also declares
  // the surface in the RenderTargets registry (geometry from the target,
  // shape "rect" unless overridden) so the device hello can announce it.
  bool addDisplay(const char* displayName, LgfxTarget* target,
                  const char* shape = "rect") {
    if (_count >= MAX_DISPLAYS || !displayName || !target) return false;
    _slots[_count].name = displayName;
    _slots[_count].target = target;
    _slots[_count].shape = shape;
    _count++;
    RenderTargets::add(displayName, target->width(), target->height(), shape,
                       RenderTargets::MODULE_LGFX);
    return true;
  }

  // Sprite-backed targets often have no geometry until the driver's begin()
  // creates the framebuffer (addDisplay typically runs before setup()).
  // Re-read it here: extension begin() runs before any hello can drain.
  void begin() override {
    for (int i = 0; i < _count; i++) {
      RenderTargets::add(_slots[i].name, _slots[i].target->width(),
                         _slots[i].target->height(), _slots[i].shape,
                         RenderTargets::MODULE_LGFX);
    }
  }

  void registerModule(LuaModule& m) override {
    m.method<LgfxModule, &LgfxModule::bind>("bind");
    // Text datum constants — the TFT_eSPI-inherited values LovyanGFX uses.
    m.constant("TL_DATUM", 0.0);  m.constant("TC_DATUM", 1.0);  m.constant("TR_DATUM", 2.0);
    m.constant("ML_DATUM", 4.0);  m.constant("MC_DATUM", 5.0);  m.constant("MR_DATUM", 6.0);
    m.constant("BL_DATUM", 8.0);  m.constant("BC_DATUM", 9.0);  m.constant("BR_DATUM", 10.0);
    m.constant("L_BASELINE", 16.0); m.constant("C_BASELINE", 17.0); m.constant("R_BASELINE", 18.0);
  }

  // lgfx.bind(name) -> drawing handle (methods below, colon-call: g:fillRect(...)).
  // Unknown name raises a Lua error — caught by the app compile/init/tick
  // pcall and reported like any other app error.
  int bind(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    LgfxTarget* t = nullptr;
    for (int i = 0; i < _count; i++) {
      if (strcmp(_slots[i].name, name) == 0) { t = _slots[i].target; break; }
    }
    if (!t) return luaL_error(L, "lgfx.bind: no display named '%s'", name);

    struct Entry { const char* name; lua_CFunction fn; };
    static const Entry entries[] = {
      {"fillScreen", l_fillScreen},
      {"drawPixel", l_drawPixel},
      {"drawLine", l_drawLine},
      {"drawRect", l_drawRect},
      {"fillRect", l_fillRect},
      {"drawRoundRect", l_drawRoundRect},
      {"fillRoundRect", l_fillRoundRect},
      {"drawCircle", l_drawCircle},
      {"fillCircle", l_fillCircle},
      {"drawTriangle", l_drawTriangle},
      {"fillTriangle", l_fillTriangle},
      {"setTextColor", l_setTextColor},
      {"setTextSize", l_setTextSize},
      {"setTextDatum", l_setTextDatum},
      {"setCursor", l_setCursor},
      {"print", l_print},
      {"drawString", l_drawString},
      {"width", l_width},
      {"height", l_height},
      {"flip", l_flip},
    };
    lua_createtable(L, 0, (int)(sizeof(entries) / sizeof(entries[0])));
    for (const Entry& e : entries) {
      lua_pushlightuserdata(L, t);
      lua_pushcclosure(L, e.fn, 1);
      lua_setfield(L, -2, e.name);
    }
    return 1;
  }

private:
  struct Slot {
    const char* name = nullptr;
    LgfxTarget* target = nullptr;
    const char* shape = "rect";
  };
  Slot _slots[MAX_DISPLAYS] = {};
  int _count = 0;

  // Colon-call convention: arg 1 is the bound handle table; drawing args
  // start at index 2. The target rides in upvalue 1.
  static LgfxTarget* tgt(lua_State* L) {
    return (LgfxTarget*)lua_touserdata(L, lua_upvalueindex(1));
  }
  static int32_t geti(lua_State* L, int idx) { return (int32_t)luaL_checkinteger(L, idx); }
  static uint32_t getc(lua_State* L, int idx) { return (uint32_t)luaL_checkinteger(L, idx); }

  static int l_fillScreen(lua_State* L) { tgt(L)->fillScreen(getc(L, 2)); return 0; }
  static int l_drawPixel(lua_State* L) {
    tgt(L)->drawPixel(geti(L, 2), geti(L, 3), getc(L, 4)); return 0;
  }
  static int l_drawLine(lua_State* L) {
    tgt(L)->drawLine(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5), getc(L, 6)); return 0;
  }
  static int l_drawRect(lua_State* L) {
    tgt(L)->drawRect(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5), getc(L, 6)); return 0;
  }
  static int l_fillRect(lua_State* L) {
    tgt(L)->fillRect(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5), getc(L, 6)); return 0;
  }
  static int l_drawRoundRect(lua_State* L) {
    tgt(L)->drawRoundRect(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5), geti(L, 6), getc(L, 7));
    return 0;
  }
  static int l_fillRoundRect(lua_State* L) {
    tgt(L)->fillRoundRect(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5), geti(L, 6), getc(L, 7));
    return 0;
  }
  static int l_drawCircle(lua_State* L) {
    tgt(L)->drawCircle(geti(L, 2), geti(L, 3), geti(L, 4), getc(L, 5)); return 0;
  }
  static int l_fillCircle(lua_State* L) {
    tgt(L)->fillCircle(geti(L, 2), geti(L, 3), geti(L, 4), getc(L, 5)); return 0;
  }
  static int l_drawTriangle(lua_State* L) {
    tgt(L)->drawTriangle(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5),
                         geti(L, 6), geti(L, 7), getc(L, 8));
    return 0;
  }
  static int l_fillTriangle(lua_State* L) {
    tgt(L)->fillTriangle(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5),
                         geti(L, 6), geti(L, 7), getc(L, 8));
    return 0;
  }
  static int l_setTextColor(lua_State* L) {
    bool hasBg = lua_gettop(L) >= 3 && !lua_isnil(L, 3);
    tgt(L)->setTextColor(getc(L, 2), hasBg ? getc(L, 3) : 0, hasBg);
    return 0;
  }
  static int l_setTextSize(lua_State* L) {
    tgt(L)->setTextSize((float)luaL_checknumber(L, 2)); return 0;
  }
  static int l_setTextDatum(lua_State* L) {
    tgt(L)->setTextDatum((uint8_t)luaL_checkinteger(L, 2)); return 0;
  }
  static int l_setCursor(lua_State* L) {
    tgt(L)->setCursor(geti(L, 2), geti(L, 3)); return 0;
  }
  static int l_print(lua_State* L) { tgt(L)->print(luaL_checkstring(L, 2)); return 0; }
  static int l_drawString(lua_State* L) {
    tgt(L)->drawString(luaL_checkstring(L, 2), geti(L, 3), geti(L, 4)); return 0;
  }
  static int l_width(lua_State* L) { lua_pushinteger(L, tgt(L)->width()); return 1; }
  static int l_height(lua_State* L) { lua_pushinteger(L, tgt(L)->height()); return 1; }
  static int l_flip(lua_State* L) { tgt(L)->flip(); return 0; }
};

} // namespace Resident

#endif // RESIDENT_LGFX_MODULE_H
