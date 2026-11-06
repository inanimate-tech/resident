// src/ResidentLgfxModule.h — the `lgfx` Lua module: resident's honest
// drawing library.
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
// - LgfxSpriteTarget<TSprite>: the R19 shape. The MODULE owns the frame
//   buffer: on the first bind it creates the sprite at the registered
//   PanelTarget's geometry, and g:flip() presents it with ONE blit through
//   that panel. The board supplies only the sprite object (the concrete
//   LovyanGFX type it can name) and the panel.
// - LgfxModule: the optional Extension. Firmware registers displays at
//   setup (addDisplay) and lists the module in SandboxConfig::extensions;
//   Lua binds by name.
//
// Present-to-glass: g:flip() presents the frame — but only while THIS module
// owns the target. `lgfx.bind(name)` claims it (R19, see
// ResidentRenderTargets.h); after `lvgl.bind(name)` takes the claim, or
// after an app reset releases it, flip is dropped silently instead of
// fighting the other library for the glass.
//
// The present path itself: if the target exposes pixels() and the registry
// has a PanelTarget for the name, the module blits the whole frame; else it
// falls back to the target's own flip() (the legacy presenter-callback
// adapter, and direct-to-panel targets where drawing is already live).
//
// Deliberately small this pass: default font + setTextSize multiplier only,
// no font selection, no Lua-created sprites. One image call: pushImage, a
// block of RGB565 pixels from a Lua string, for apps that keep their own
// picture and send what changed in one call instead of a fillRect a run; or,
// with a palette, a block of palette indices with one of them transparent, so
// an app can lay sprites over a backdrop, clipped to a rectangle
// (setClipRect), and leave every pixel's work to C.
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

  // A block of w x h pixels at x, y: RGB565, big-endian (high byte first),
  // row by row. The default draws it a pixel at a time, so every target has
  // it; a target over LovyanGFX hands it to pushImage.
  virtual void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* rgb565be) {
    int32_t c0, c1, r0, r1;
    if (!visible(x, y, w, h, c0, c1, r0, r1)) return;
    for (int32_t r = r0; r < r1; r++) {
      for (int32_t c = c0; c < c1; c++) {
        drawPixel(x + c, y + r, rgb888(rgb565be + ((size_t)r * (size_t)w + (size_t)c) * 2));
      }
    }
  }

  // An indexed block: w x h bytes, row by row, each an index into palette
  // (ncolors colours, RGB565 big-endian). An index equal to transparent (-1
  // for none) or past the palette's end isn't drawn. The default draws a
  // pixel at a time; a target with a frame buffer should write it straight in.
  virtual void pushIndexed(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* idx,
                           const uint8_t* palette, int32_t ncolors, int32_t transparent) {
    int32_t c0, c1, r0, r1;
    if (!visible(x, y, w, h, c0, c1, r0, r1)) return;
    for (int32_t r = r0; r < r1; r++) {
      const uint8_t* row = idx + (size_t)r * (size_t)w;
      for (int32_t c = c0; c < c1; c++) {
        int32_t i = row[c];
        if (i != transparent && i < ncolors) drawPixel(x + c, y + r, rgb888(palette + i * 2));
      }
    }
  }

  // Drawing limited to a rectangle until clearClipRect(), as LovyanGFX's. The
  // base keeps it and clips the blocks its own pushImage and pushIndexed draw;
  // a target over LovyanGFX passes it on, so everything it draws is clipped; a
  // target with a frame buffer of its own should clip everything it draws.
  // App reset clears it.
  virtual void setClipRect(int32_t x, int32_t y, int32_t w, int32_t h) {
    _clipped = true;
    _clipX0 = x; _clipY0 = y; _clipX1 = x + w; _clipY1 = y + h;
  }
  virtual void clearClipRect() { _clipped = false; }

  // R19 machinery hooks. Defaults keep pre-R19 targets (their own presenter,
  // or direct-to-panel) working unchanged.
  //
  // allocate(): called on every bind, at the panel's geometry — create the
  // frame buffer here if it doesn't exist yet. Must be idempotent.
  virtual bool allocate(int32_t /*w*/, int32_t /*h*/) { return true; }
  // The frame buffer, RGB565 big-endian (the PanelTarget::blit contract), or
  // null when this target has no readable buffer (present via flip()).
  virtual const uint16_t* pixels() const { return nullptr; }

  // Present the frame to the glass. Only called when pixels() is null (or no
  // panel is registered); default: drawing is live, nothing to do.
  virtual void flip() {}

protected:
  // The part of a w x h block at x, y inside the clip, as the block's own
  // columns c0..c1 and rows r0..r1 (ends exclusive); false when none of it is.
  bool visible(int32_t x, int32_t y, int32_t w, int32_t h,
               int32_t& c0, int32_t& c1, int32_t& r0, int32_t& r1) const {
    c0 = 0; r0 = 0; c1 = w; r1 = h;
    if (_clipped) {
      if (_clipX0 > x) c0 = _clipX0 - x;
      if (_clipY0 > y) r0 = _clipY0 - y;
      if (_clipX1 < x + w) c1 = _clipX1 - x;
      if (_clipY1 < y + h) r1 = _clipY1 - y;
    }
    return c0 < c1 && r0 < r1;
  }
  static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
  static uint32_t rgb888(const uint8_t* be) {
    uint16_t v = be16(be);
    uint32_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
    return ((r5 << 3 | r5 >> 2) << 16) | ((g6 << 2 | g6 >> 4) << 8) | (b5 << 3 | b5 >> 2);
  }

  bool _clipped = false;
  int32_t _clipX0 = 0, _clipY0 = 0, _clipX1 = 0, _clipY1 = 0;
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

  // LovyanGFX's pushImage(x, y, w, h, const uint16_t*) reads native-endian
  // RGB565; the Lua string is big-endian, so rows go through a small buffer,
  // as many whole rows at a time as fit.
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* be) override {
    static constexpr int32_t N = 1024;
    static uint16_t buf[N];
    if (w > N) { LgfxTarget::pushImage(x, y, w, h, be); return; }
    const int32_t rowsPer = N / w;
    for (int32_t r0 = 0; r0 < h; r0 += rowsPer) {
      const int32_t rows = (h - r0 < rowsPer) ? h - r0 : rowsPer;
      const uint8_t* p = be + (size_t)r0 * (size_t)w * 2;
      for (int32_t i = 0; i < rows * w; i++) buf[i] = (uint16_t)((p[2 * i] << 8) | p[2 * i + 1]);
      _gfx->pushImage(x, y + r0, w, rows, buf);
    }
  }

  // An indexed block goes to LovyanGFX a run of drawn pixels at a time, as
  // native-endian RGB565 through a small buffer; LovyanGFX does the clipping.
  void pushIndexed(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* idx,
                   const uint8_t* palette, int32_t ncolors, int32_t transparent) override {
    static constexpr int32_t N = 1024;
    static uint16_t buf[N];
    int32_t c0, c1, r0, r1;
    if (!this->visible(x, y, w, h, c0, c1, r0, r1)) return;
    for (int32_t r = r0; r < r1; r++) {
      const uint8_t* row = idx + (size_t)r * (size_t)w;
      int32_t c = c0;
      while (c < c1) {
        while (c < c1 && (row[c] == transparent || row[c] >= ncolors)) c++;
        const int32_t start = c;
        int32_t n = 0;
        while (c < c1 && n < N && row[c] != transparent && row[c] < ncolors) buf[n++] = this->be16(palette + row[c++] * 2);
        if (n) _gfx->pushImage(x + start, y + r, n, 1, buf);
      }
    }
  }

  void setClipRect(int32_t x, int32_t y, int32_t w, int32_t h) override {
    LgfxTarget::setClipRect(x, y, w, h);
    _gfx->setClipRect(x, y, w, h);
  }
  void clearClipRect() override {
    LgfxTarget::clearClipRect();
    _gfx->clearClipRect();
  }

  void flip() override { if (_present) _present(); }

protected:
  TGfx* _gfx;

private:
  Presenter _present;
};

// ── R19: a module-owned sprite over a registered PanelTarget ─────────────
//
// The board declares the sprite object (only it can name the concrete
// LovyanGFX type) and hands it over; the MODULE decides when it exists and
// how it reaches the glass:
//
//   M5Canvas canvas{&M5.Display};
//   Resident::LgfxSpriteTarget<M5Canvas> lgfxMain{&canvas};
//   Resident::RenderTargets::addPanel("main", &myPanel);
//   lgfxModule.addDisplay("main", &lgfxMain);
//
// createSprite is idempotent here: a board that pre-creates the sprite for
// its own status-display path (the m5stick DisplayDriver does) keeps the one
// frame buffer, and lgfx composes on top of it.
template <class TSprite>
class LgfxSpriteTarget : public LgfxLovyanTarget<TSprite> {
public:
  explicit LgfxSpriteTarget(TSprite* sprite)
      : LgfxLovyanTarget<TSprite>(sprite) {}

  bool allocate(int32_t w, int32_t h) override {
    if (this->_gfx->getBuffer()) return true;   // already created
    this->_gfx->setColorDepth(16);              // must precede createSprite
    return this->_gfx->createSprite(w, h) != nullptr;
  }

  const uint16_t* pixels() const override {
    return (const uint16_t*)this->_gfx->getBuffer();
  }
};

// ── The Lua module ────────────────────────────────────────────────────────
class LgfxModule : public Extension {
public:
  static constexpr int MAX_DISPLAYS = 4;

  const char* name() const override { return "lgfx"; }

  // Firmware setup: register a display under a bind name. Call before
  // Sandbox::setup(). Returns false when the table is full. Declares the
  // MODULE_LGFX bit on the target in the RenderTargets registry; geometry
  // comes from the registered PanelTarget when the board registered one
  // (addPanel), else from the drawing target itself.
  //
  // `shape` applies ONLY to a target with no PanelTarget behind it — a bare
  // sprite, where nothing else can say. When the board registered a panel,
  // that panel's shape is the truth and this argument is ignored: shape is a
  // physical fact about the glass, and the board states it once.
  bool addDisplay(const char* displayName, LgfxTarget* target,
                  const char* shape = "rect") {
    if (_count >= MAX_DISPLAYS || !displayName || !target) return false;
    _slots[_count].name = displayName;
    _slots[_count].target = target;
    _slots[_count].shape = shape;
    _count++;
    declare(_slots[_count - 1]);
    return true;
  }

  // Sprite-backed targets often have no geometry until the panel is up
  // (addDisplay typically runs before setup(), sometimes before the board's
  // display begin()). Re-read it here.
  // begin() runs after every driver's begin(), so hardware is up: this is the
  // first safe moment to measure. A panel-backed target needs no cache (the
  // registry reads its panel), but a sprite-backed one has no panel to ask, so
  // record its geometry here for readers like the `screens` module.
  void begin() override {
    for (int i = 0; i < _count; i++) {
      const Slot& s = _slots[i];
      declare(s);
      if (!RenderTargets::panel(s.name) && s.target) {
        RenderTargets::add(s.name, s.target->width(), s.target->height(),
                           s.shape, RenderTargets::MODULE_LGFX);
      }
    }
  }

  // Bind is the claim, app reset is the release: the next app's first bind
  // is a clean claim, and until then this module's flips stand down.
  void onAppReset() override {
    RenderTargets::release(RenderTargets::MODULE_LGFX);
    for (int i = 0; i < _count; i++) _slots[i].target->clearClipRect();   // the next app draws everywhere
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
    Slot* s = nullptr;
    for (int i = 0; i < _count; i++) {
      if (strcmp(_slots[i].name, name) == 0) { s = &_slots[i]; break; }
    }
    if (!s) return luaL_error(L, "lgfx.bind: no display named '%s'", name);

    // R19: the bind is the claim. Build our machinery over the panel (the
    // frame buffer, at the panel's geometry) and take ownership — the other
    // module's present path stands down from here.
    PanelTarget* p = RenderTargets::panel(name);
    int32_t w = p ? p->width() : s->target->width();
    int32_t h = p ? p->height() : s->target->height();
    if (!s->target->allocate(w, h)) {
      return luaL_error(L, "lgfx.bind: '%s' frame buffer allocation failed", name);
    }
    declare(*s);
    RenderTargets::claim(name, RenderTargets::MODULE_LGFX);

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
      {"pushImage", l_pushImage},
      {"setClipRect", l_setClipRect},
      {"clearClipRect", l_clearClipRect},
      {"width", l_width},
      {"height", l_height},
      {"flip", l_flip},
    };
    lua_createtable(L, 0, (int)(sizeof(entries) / sizeof(entries[0])));
    for (const Entry& e : entries) {
      lua_pushlightuserdata(L, s);
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

  // Registry declaration for one slot: the module bit and the shape, never
  // geometry. addDisplay is called from a board's config function, which
  // commonly runs during static init — measuring anything there dereferences
  // hardware that does not exist yet. Readers get live geometry from
  // RenderTargets::size().
  static void declare(const Slot& s) {
    RenderTargets::declare(s.name, RenderTargets::MODULE_LGFX, s.shape);
  }

  // Present one frame — the R19 gate. Not the owner? Drop it silently: the
  // other library is driving this panel and a flip here would strobe.
  static void present(const Slot& s) {
    if (!RenderTargets::isOwner(s.name, RenderTargets::MODULE_LGFX)) return;
    PanelTarget* p = RenderTargets::panel(s.name);
    const uint16_t* px = s.target->pixels();
    if (p && px) {
      p->blit(0, 0, s.target->width(), s.target->height(), px);
      p->frameDone();                  // one flip is one whole frame
    } else {
      s.target->flip();                // legacy presenter / direct-to-panel
    }
  }

  // Colon-call convention: arg 1 is the bound handle table; drawing args
  // start at index 2. The bound slot rides in upvalue 1.
  static Slot* slot(lua_State* L) {
    return (Slot*)lua_touserdata(L, lua_upvalueindex(1));
  }
  static LgfxTarget* tgt(lua_State* L) { return slot(L)->target; }
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
  // g:pushImage(x, y, w, h, data[, palette[, transparent]]): without a
  // palette, data is exactly w * h * 2 bytes, RGB565 big-endian, row by row.
  // With one (a string of 1 to 256 colours, RGB565 big-endian, 2 bytes each),
  // data is exactly w * h bytes, each an index into it; an index equal to
  // transparent (0-255) or past the palette's end isn't drawn.
  static int l_pushImage(lua_State* L) {
    int32_t x = geti(L, 2), y = geti(L, 3), w = geti(L, 4), h = geti(L, 5);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 6, &len);
    if (lua_isnoneornil(L, 7)) {
      if (w <= 0 || h <= 0) return 0;
      if (w > 4096 || h > 4096 || len != (size_t)w * (size_t)h * 2) {
        return luaL_argerror(L, 6, "expected w * h * 2 bytes of RGB565, big-endian");
      }
      tgt(L)->pushImage(x, y, w, h, (const uint8_t*)data);
      return 0;
    }
    size_t plen = 0;
    const char* palette = luaL_checklstring(L, 7, &plen);
    int32_t transparent = (int32_t)luaL_optinteger(L, 8, -1);
    if (plen == 0 || plen > 512 || plen % 2 != 0) {
      return luaL_argerror(L, 7, "expected 1 to 256 colours of RGB565, big-endian");
    }
    if (w <= 0 || h <= 0) return 0;
    if (w > 4096 || h > 4096 || len != (size_t)w * (size_t)h) {
      return luaL_argerror(L, 6, "expected w * h bytes of palette indices");
    }
    tgt(L)->pushIndexed(x, y, w, h, (const uint8_t*)data, (const uint8_t*)palette,
                        (int32_t)(plen / 2), transparent);
    return 0;
  }
  static int l_setClipRect(lua_State* L) {
    tgt(L)->setClipRect(geti(L, 2), geti(L, 3), geti(L, 4), geti(L, 5)); return 0;
  }
  static int l_clearClipRect(lua_State* L) { tgt(L)->clearClipRect(); return 0; }
  static int l_width(lua_State* L) { lua_pushinteger(L, tgt(L)->width()); return 1; }
  static int l_height(lua_State* L) { lua_pushinteger(L, tgt(L)->height()); return 1; }
  static int l_flip(lua_State* L) { present(*slot(L)); return 0; }
};

} // namespace Resident

#endif // RESIDENT_LGFX_MODULE_H
