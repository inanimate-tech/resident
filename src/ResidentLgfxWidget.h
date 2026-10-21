// src/ResidentLgfxWidget.h — the Lgfx lvgl widget: LovyanGFX drawing, by the
// app's own Lua function, as an ordinary widget —
//
//   c:Lgfx{ w = 128, h = 46, fps = 20, fn = function(g, t)
//     g:fillScreen(0x000000)
//     g:drawLine(0, 0, 127, 45, 0xffffff)
//   end }
//
// fn is called once a frame (fps, default 15; 0 = only when a key is set)
// with a pen, g, and t in seconds. It draws with the Adafruit GFX / LovyanGFX
// method set — fillScreen, drawPixel, drawLine, drawFastHLine/VLine,
// draw/fillRect, draw/fillRoundRect, draw/fillCircle, draw/fillEllipse,
// draw/fillTriangle, draw/fillArc, setTextColor/Size/Datum, setCursor,
// print, drawString, width, height, color888 — into a sprite the widget's
// size, which is then drawn as the widget's image. The sprite keeps its
// pixels between frames: fn clears it, or leaves trails. Coordinates may be
// fractional (they round); colours are 0xRRGGBB or "#RRGGBB". Lines are
// LovyanGFX's, not anti-aliased, so on a one-bit panel a 1 px line is 1 px.
// The widget is opaque unless `transparent` names a colour (as LovyanGFX's
// pushSprite does): pixels of that colour show what is behind it.
// Everything around the widget — labels, fonts, the theme — is lvgl's.
//
// A frame widget (ResidentFrameWidget.h: the timer, the owed frame, the
// buffer, the draw), so fn runs at most once per owed frame, in
// LV_EVENT_DRAW_MAIN, and the sprite becomes the widget's buffer.
//
// LovyanGFX comes in two copies (LovyanGFX itself, and the one inside M5GFX),
// so the sprite type is the board's, named where it is already included, as
// ResidentLgfxModule.h's display type is:
//
//   #include <LovyanGFX.hpp>
//   #include <ResidentLgfxWidget.h>
//   lvglModule.addWidget(Resident::lgfxw::installWidget<lgfx::LGFX_Sprite>);   // or <M5Canvas>
//
// OPT-IN, like ResidentLvglModule.h, and on a build without LVGL/luavgl it
// compiles to nothing.
#ifndef RESIDENT_LGFX_WIDGET_H
#define RESIDENT_LGFX_WIDGET_H

#if __has_include(<lvgl.h>) && __has_include(<luavgl.h>)

#include <cstdint>
#include <cstring>

#include "ResidentFrameWidget.h"
#include "ResidentLgfxWidgetCore.h"

namespace Resident {
namespace lgfxw {

// The drawing surface fn draws on. Colours are 0xRRGGBB.
class Canvas {
public:
  virtual ~Canvas() = default;
  // (Re)allocate at w x h; keep the pixels when the size is unchanged.
  virtual bool resize(int32_t w, int32_t h) = 0;
  // RGB565, byte-swapped (LovyanGFX's sprite order), width() * height().
  virtual const uint16_t* pixels() const = 0;
  virtual int32_t width() = 0;
  virtual int32_t height() = 0;

  virtual void fillScreen(uint32_t c) = 0;
  virtual void drawPixel(int32_t x, int32_t y, uint32_t c) = 0;
  virtual void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) = 0;
  virtual void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) = 0;
  virtual void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) = 0;
  virtual void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) = 0;
  virtual void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) = 0;
  virtual void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) = 0;
  virtual void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) = 0;
  virtual void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) = 0;
  virtual void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) = 0;
  virtual void drawEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c) = 0;
  virtual void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c) = 0;
  virtual void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint32_t c) = 0;
  virtual void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                            int32_t x2, int32_t y2, uint32_t c) = 0;
  virtual void drawArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, uint32_t c) = 0;
  virtual void fillArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, uint32_t c) = 0;

  virtual void setTextColor(uint32_t fg) = 0;
  virtual void setTextColor(uint32_t fg, uint32_t bg) = 0;
  virtual void setTextSize(float s) = 0;
  virtual void setTextDatum(uint8_t d) = 0;
  virtual void setCursor(int32_t x, int32_t y) = 0;
  virtual void print(const char* s) = 0;
  virtual void drawString(const char* s, int32_t x, int32_t y) = 0;
};

using CanvasFactory = Canvas* (*)();

// A Canvas over any LovyanGFX-shaped sprite (LGFX_Sprite, M5Canvas).
template <class TSprite>
class SpriteCanvas : public Canvas {
public:
  bool resize(int32_t w, int32_t h) override {
    if (_s.getBuffer() && _s.width() == w && _s.height() == h) return true;
    _s.deleteSprite();
    _s.setColorDepth(16);
    _s.setPsram(w * h * 2 > 32 * 1024);   // small sprites stay in fast internal RAM
    return _s.createSprite(w, h) != nullptr;
  }
  const uint16_t* pixels() const override {
    return static_cast<const uint16_t*>(const_cast<TSprite&>(_s).getBuffer());
  }
  int32_t width() override { return _s.width(); }
  int32_t height() override { return _s.height(); }

  void fillScreen(uint32_t c) override { _s.fillScreen(c); }
  void drawPixel(int32_t x, int32_t y, uint32_t c) override { _s.drawPixel(x, y, c); }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) override {
    _s.drawLine(x0, y0, x1, y1, c);
  }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) override { _s.drawFastHLine(x, y, w, c); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) override { _s.drawFastVLine(x, y, h, c); }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override { _s.drawRect(x, y, w, h, c); }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override { _s.fillRect(x, y, w, h, c); }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) override {
    _s.drawRoundRect(x, y, w, h, r, c);
  }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) override {
    _s.fillRoundRect(x, y, w, h, r, c);
  }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) override { _s.drawCircle(x, y, r, c); }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) override { _s.fillCircle(x, y, r, c); }
  void drawEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c) override {
    _s.drawEllipse(x, y, rx, ry, c);
  }
  void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c) override {
    _s.fillEllipse(x, y, rx, ry, c);
  }
  void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint32_t c) override {
    _s.drawTriangle(x0, y0, x1, y1, x2, y2, c);
  }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint32_t c) override {
    _s.fillTriangle(x0, y0, x1, y1, x2, y2, c);
  }
  void drawArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, uint32_t c) override {
    _s.drawArc(x, y, r0, r1, a0, a1, c);
  }
  void fillArc(int32_t x, int32_t y, int32_t r0, int32_t r1, float a0, float a1, uint32_t c) override {
    _s.fillArc(x, y, r0, r1, a0, a1, c);
  }

  void setTextColor(uint32_t fg) override { _s.setTextColor(fg); }
  void setTextColor(uint32_t fg, uint32_t bg) override { _s.setTextColor(fg, bg); }
  void setTextSize(float s) override { _s.setTextSize(s); }
  void setTextDatum(uint8_t d) override { _s.setTextDatum(d); }
  void setCursor(int32_t x, int32_t y) override { _s.setCursor(x, y); }
  void print(const char* s) override { _s.print(s); }
  void drawString(const char* s, int32_t x, int32_t y) override { _s.drawString(s, x, y); }

private:
  TSprite _s;
};

template <class TSprite>
Canvas* makeSpriteCanvas() { return new SpriteCanvas<TSprite>(); }

namespace detail {

struct LgfxObj {
  framewidget::Widget base;       // first: the lv_obj_t, then the shared state
  int penRef = LUA_NOREF;         // in base.L, beside fn
  Canvas* canvas = nullptr;       // the sprite fn draws on
  uint16_t key = 0;               // the transparent colour, RGB565 in LVGL's order
  bool keyed = false;             // transparent = a colour: draw RGB565A8
  bool drawing = false;           // inside fn: the pen may draw
};

inline LgfxObj* of(framewidget::Widget* w) { return reinterpret_cast<LgfxObj*>(w); }

// The pen handed to fn. It outlives nothing: the widget's destructor cuts
// it loose, and a pen kept past its frame refuses to draw.
struct Pen { LgfxObj* s; };
inline constexpr const char* kPenMeta = "lgfx.pen";

inline lv_obj_class_t lgfxClass;
inline CanvasFactory factory = nullptr;

inline void constructor(const lv_obj_class_t*, lv_obj_t* obj) {
  LgfxObj* s = reinterpret_cast<LgfxObj*>(obj);
  s->penRef = LUA_NOREF;
  s->canvas = nullptr;
  s->key = 0;
  s->keyed = false;
  s->drawing = false;
  framewidget::construct(&s->base, "Lgfx");
}

inline void destructor(const lv_obj_class_t*, lv_obj_t* obj) {
  LgfxObj* s = reinterpret_cast<LgfxObj*>(obj);
  lua_State* L = s->base.L;
  if (L && s->penRef != LUA_NOREF) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, s->penRef);
    if (Pen* p = static_cast<Pen*>(lua_touserdata(L, -1))) p->s = nullptr;
    lua_pop(L, 1);
    luaL_unref(L, LUA_REGISTRYINDEX, s->penRef);
  }
  s->penRef = LUA_NOREF;
  delete s->canvas;
  s->canvas = nullptr;
  framewidget::destroy(&s->base);
}

// The sprite and LVGL's copy of it, sized to the widget; kept as they are on
// every frame where the size has not changed.
inline bool prepare(framewidget::Widget* w, int width, int height, bool& fresh) {
  LgfxObj* s = of(w);
  if (!s->canvas && factory) s->canvas = factory();
  if (!s->canvas || !s->canvas->resize(width, height) || !s->canvas->pixels()) return false;
  const lv_color_format_t cf = s->keyed ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
  return framewidget::ensureBuf(w, width, height, cf, fresh);
}

inline void runFrame(LgfxObj* s) {
  lua_State* L = s->base.L;
  lua_rawgeti(L, LUA_REGISTRYINDEX, s->base.fnRef);
  lua_rawgeti(L, LUA_REGISTRYINDEX, s->penRef);
  lua_pushnumber(L, framewidget::seconds(&s->base));
  s->drawing = true;
  const int rc = lua_pcall(L, 2, 0, 0);
  s->drawing = false;
  if (rc != LUA_OK) framewidget::reportError(&s->base, L);
}

// The owed frame: fn draws on the sprite, which is copied into LVGL's order.
inline void paint(framewidget::Widget* w, int width, int height) {
  LgfxObj* s = of(w);
  if (framewidget::live(w)) runFrame(s);
  // RGB565A8 is the colour plane, then an alpha plane at half its stride.
  const uint16_t* src = s->canvas->pixels();
  const uint32_t stride = w->buf->header.stride;
  uint8_t* alpha = w->buf->data + (size_t)stride * height;
  for (int y = 0; y < height; y++) {
    uint16_t* row = reinterpret_cast<uint16_t*>(w->buf->data + (size_t)y * stride);
    if (s->keyed) {
      copySwappedKeyed(src + (size_t)y * width, row, alpha + (size_t)y * (stride / 2),
                       (size_t)width, s->key);
    } else {
      copySwapped(src + (size_t)y * width, row, (size_t)width);
    }
  }
}

inline const framewidget::Kind kKind{prepare, paint};

inline void eventCb(const lv_obj_class_t*, lv_event_t* e) { framewidget::handleEvent(&lgfxClass, e, kKind); }

inline lv_obj_t* create(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_class_create_obj(&lgfxClass, parent);
  lv_obj_class_init_obj(obj);
  return obj;
}

// ── the pen: LovyanGFX's method set ────────────────────────────────────────

inline Pen* checkPen(lua_State* L) { return static_cast<Pen*>(luaL_checkudata(L, 1, kPenMeta)); }

inline Canvas* canvas(lua_State* L) {
  Pen* p = checkPen(L);
  if (!p->s || !p->s->drawing || !p->s->canvas) {
    luaL_error(L, "Lgfx: draw with g only inside fn, while it runs");
  }
  return p->s->canvas;
}

inline int32_t argPx(lua_State* L, int i) { return px(luaL_checknumber(L, i)); }
inline float argNum(lua_State* L, int i) { return (float)luaL_checknumber(L, i); }

inline uint32_t argColor(lua_State* L, int i) {
  if (lua_type(L, i) == LUA_TSTRING) {
    uint32_t v;
    if (!parseHex(lua_tostring(L, i), v)) luaL_error(L, "Lgfx: a colour is 0xRRGGBB or \"#RRGGBB\"");
    return v;
  }
  return (uint32_t)luaL_checkinteger(L, i) & 0xFFFFFFu;
}

// ── the widget's keys ──────────────────────────────────────────────────────

inline int setKv(lua_State* L, void* data) {
  LgfxObj* s = static_cast<LgfxObj*>(data);
  const char* k = lua_tostring(L, -2);
  if (std::strcmp(k, "fn") == 0) {
    framewidget::setFn(&s->base, L);
    if (s->penRef == LUA_NOREF) {
      Pen* p = static_cast<Pen*>(lua_newuserdata(L, sizeof(Pen)));
      p->s = s;
      luaL_setmetatable(L, kPenMeta);
      s->penRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  } else if (std::strcmp(k, "transparent") == 0) {
    // transparent = a colour: pixels of it show what is behind (LovyanGFX's
    // pushSprite(x, y, transparent)); false or nil: opaque.
    s->keyed = !lua_isnil(L, -1) && !(lua_isboolean(L, -1) && !lua_toboolean(L, -1));
    if (s->keyed) s->key = rgb565(argColor(L, -1));
  } else if (std::strcmp(k, "fps") == 0) {
    framewidget::setFps(&s->base, luaL_checkinteger(L, -1));
    return 0;
  } else {
    return luavgl_obj_set_property_kv(L, &s->base.obj);
  }
  framewidget::owe(&s->base);
  return 0;
}

inline int luaSet(lua_State* L) { return framewidget::set(L, setKv); }

inline int luaCreate(lua_State* L) { return luavgl_obj_create_helper(L, create); }

inline const rotable_Reg kMethods[] = {
    {"set", LUA_TFUNCTION, {luaSet}},
    {0, 0, {0}},
};

inline int l_fillScreen(lua_State* L) { canvas(L)->fillScreen(argColor(L, 2)); return 0; }
inline int l_drawPixel(lua_State* L) { canvas(L)->drawPixel(argPx(L, 2), argPx(L, 3), argColor(L, 4)); return 0; }
inline int l_drawLine(lua_State* L) {
  canvas(L)->drawLine(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argColor(L, 6)); return 0;
}
inline int l_drawFastHLine(lua_State* L) { canvas(L)->drawFastHLine(argPx(L, 2), argPx(L, 3), argPx(L, 4), argColor(L, 5)); return 0; }
inline int l_drawFastVLine(lua_State* L) { canvas(L)->drawFastVLine(argPx(L, 2), argPx(L, 3), argPx(L, 4), argColor(L, 5)); return 0; }
inline int l_drawRect(lua_State* L) { canvas(L)->drawRect(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argColor(L, 6)); return 0; }
inline int l_fillRect(lua_State* L) { canvas(L)->fillRect(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argColor(L, 6)); return 0; }
inline int l_drawRoundRect(lua_State* L) {
  canvas(L)->drawRoundRect(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argPx(L, 6), argColor(L, 7)); return 0;
}
inline int l_fillRoundRect(lua_State* L) {
  canvas(L)->fillRoundRect(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argPx(L, 6), argColor(L, 7)); return 0;
}
inline int l_drawCircle(lua_State* L) { canvas(L)->drawCircle(argPx(L, 2), argPx(L, 3), argPx(L, 4), argColor(L, 5)); return 0; }
inline int l_fillCircle(lua_State* L) { canvas(L)->fillCircle(argPx(L, 2), argPx(L, 3), argPx(L, 4), argColor(L, 5)); return 0; }
inline int l_drawEllipse(lua_State* L) {
  canvas(L)->drawEllipse(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argColor(L, 6)); return 0;
}
inline int l_fillEllipse(lua_State* L) {
  canvas(L)->fillEllipse(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argColor(L, 6)); return 0;
}
inline int l_drawTriangle(lua_State* L) {
  canvas(L)->drawTriangle(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argPx(L, 6), argPx(L, 7), argColor(L, 8)); return 0;
}
inline int l_fillTriangle(lua_State* L) {
  canvas(L)->fillTriangle(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argPx(L, 6), argPx(L, 7), argColor(L, 8)); return 0;
}
inline int l_drawArc(lua_State* L) {
  canvas(L)->drawArc(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argNum(L, 6), argNum(L, 7), argColor(L, 8)); return 0;
}
inline int l_fillArc(lua_State* L) {
  canvas(L)->fillArc(argPx(L, 2), argPx(L, 3), argPx(L, 4), argPx(L, 5), argNum(L, 6), argNum(L, 7), argColor(L, 8)); return 0;
}
inline int l_setTextColor(lua_State* L) {
  Canvas* c = canvas(L);
  if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) c->setTextColor(argColor(L, 2), argColor(L, 3));
  else c->setTextColor(argColor(L, 2));
  return 0;
}
inline int l_setTextSize(lua_State* L) { canvas(L)->setTextSize(argNum(L, 2)); return 0; }
inline int l_setTextDatum(lua_State* L) { canvas(L)->setTextDatum((uint8_t)luaL_checkinteger(L, 2)); return 0; }
inline int l_setCursor(lua_State* L) { canvas(L)->setCursor(argPx(L, 2), argPx(L, 3)); return 0; }
inline int l_print(lua_State* L) { canvas(L)->print(luaL_tolstring(L, 2, nullptr)); return 0; }
inline int l_drawString(lua_State* L) {
  canvas(L)->drawString(luaL_tolstring(L, 2, nullptr), argPx(L, 3), argPx(L, 4)); return 0;
}
inline int l_width(lua_State* L) { lua_pushinteger(L, canvas(L)->width()); return 1; }
inline int l_height(lua_State* L) { lua_pushinteger(L, canvas(L)->height()); return 1; }
inline int l_color888(lua_State* L) {
  // g:color888(r, g, b), 0..255 each -> 0xRRGGBB
  auto b8 = [&](int i) { lua_Integer v = luaL_checkinteger(L, i); return (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
  lua_pushinteger(L, (lua_Integer)((b8(2) << 16) | (b8(3) << 8) | b8(4)));
  return 1;
}

inline const luaL_Reg kPen[] = {
    {"fillScreen", l_fillScreen}, {"drawPixel", l_drawPixel}, {"drawLine", l_drawLine},
    {"drawFastHLine", l_drawFastHLine}, {"drawFastVLine", l_drawFastVLine},
    {"drawRect", l_drawRect}, {"fillRect", l_fillRect},
    {"drawRoundRect", l_drawRoundRect}, {"fillRoundRect", l_fillRoundRect},
    {"drawCircle", l_drawCircle}, {"fillCircle", l_fillCircle},
    {"drawEllipse", l_drawEllipse}, {"fillEllipse", l_fillEllipse},
    {"drawTriangle", l_drawTriangle}, {"fillTriangle", l_fillTriangle},
    {"drawArc", l_drawArc}, {"fillArc", l_fillArc},
    {"setTextColor", l_setTextColor}, {"setTextSize", l_setTextSize},
    {"setTextDatum", l_setTextDatum}, {"setCursor", l_setCursor},
    {"print", l_print}, {"drawString", l_drawString},
    {"width", l_width}, {"height", l_height}, {"color888", l_color888},
    {nullptr, nullptr},
};

// Text datums, the TFT_eSPI values LovyanGFX keeps: g.MC_DATUM.
struct Datum { const char* name; int v; };
inline const Datum kDatums[] = {
    {"TL_DATUM", 0}, {"TC_DATUM", 1}, {"TR_DATUM", 2},
    {"ML_DATUM", 4}, {"MC_DATUM", 5}, {"MR_DATUM", 6},
    {"BL_DATUM", 8}, {"BC_DATUM", 9}, {"BR_DATUM", 10},
    {"L_BASELINE", 16}, {"C_BASELINE", 17}, {"R_BASELINE", 18},
};

}  // namespace detail

// The non-template half of the installer: the class, the pen's metatable,
// and the constructor `Lgfx` in luavgl's widgets table.
inline void installWith(lua_State* L, CanvasFactory make) {
  static bool classReady = false;
  if (!classReady) {
    framewidget::defineClass(detail::lgfxClass, "lgfx", sizeof(detail::LgfxObj),
                             detail::constructor, detail::destructor, detail::eventCb);
    classReady = true;
  }
  detail::factory = make;
  luavgl_obj_newmetatable(L, &detail::lgfxClass, "lv_lgfx", detail::kMethods);
  lua_pop(L, 1);
  luaL_getmetatable(L, "widgets");
  lua_pushcfunction(L, detail::luaCreate);
  lua_setfield(L, -2, "Lgfx");
  lua_pop(L, 1);
  // The pen's metatable: its methods and the datums, by __index.
  if (luaL_newmetatable(L, detail::kPenMeta)) {
    luaL_newlib(L, detail::kPen);
    for (const detail::Datum& d : detail::kDatums) {
      lua_pushinteger(L, d.v);
      lua_setfield(L, -2, d.name);
    }
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, "Lgfx pen");
    lua_setfield(L, -2, "__metatable");
  }
  lua_pop(L, 1);
}

// The board's installer:
// lvglModule.addWidget(Resident::lgfxw::installWidget<lgfx::LGFX_Sprite>).
template <class TSprite>
void installWidget(lua_State* L) { installWith(L, &makeSpriteCanvas<TSprite>); }

}  // namespace lgfxw
}  // namespace Resident

#endif  // __has_include(<lvgl.h>) && __has_include(<luavgl.h>)
#endif  // RESIDENT_LGFX_WIDGET_H
