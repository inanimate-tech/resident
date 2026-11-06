// src/ResidentShaderWidget.h — the Shader lvgl widget: the app's own Lua
// function, called per cell, as an ordinary widget —
//
//   c:Shader{ palette = "ember", fn = function(x, y, t, u, v)
//     return shader.fbm(x * 3, y * 3 + t)        -- one value 0..1: the palette
//   end }                                         -- or r, g, b (0..1)
//
// and the `shader` helpers (fract, mix, clamp, step, smoothstep, mod, length,
// hash, noise, fbm, hsv, circle, box, segment, smin, rot). A board offers
// both with
//
//   #include <ResidentShaderWidget.h>
//   lvglModule.addWidget(Resident::shader::installWidget);
//
// Keys, besides every base-object property (x, y, w, h, align, radius, opa,
// ...): fn, palette (a name — ember, ocean, aurora, sunset, rainbow, mono —
// or 2..8 colours), speed (1: t in seconds; 0 freezes), scale (1; 2 zooms
// in), cell (block size in pixels; 0 = auto, coarsening if a frame runs over
// budget) and fps (15; 0 = draw once, for e-paper). It fills its parent
// unless sized. x, y are centred with the shorter side -1..1 and y up; u, v
// run 0..1. See ResidentShaderCore.h.
//
// The program is the app's own Lua function, so it hot-reloads like the rest
// of the app; this file is everything around it. A frame widget
// (ResidentFrameWidget.h: the timer, the owed frame, the buffer, the draw),
// so it makes a frame at most once per owed frame, in LV_EVENT_DRAW_MAIN: the
// function is called once per cell, and the results become the widget's
// buffer, each cell a block.
//
// OPT-IN, like ResidentLvglModule.h, and on a build without LVGL/luavgl it
// compiles to nothing.
#ifndef RESIDENT_SHADER_WIDGET_H
#define RESIDENT_SHADER_WIDGET_H

#if __has_include(<lvgl.h>) && __has_include(<luavgl.h>)

#include <Arduino.h>   // micros(): the frame budget
#include <cstring>
#include <new>

#include "ResidentFrameWidget.h"
#include "ResidentShaderCore.h"

namespace Resident {
namespace shader {
namespace detail {

struct ShaderObj {
  framewidget::Widget base;       // first: the lv_obj_t, then the shared state
  Palette palette{};
  uint16_t* cells = nullptr;      // one RGB565 colour per cell
  int cellsW = 0, cellsH = 0, cellNow = 0;
  float speed = 1, scale = 1;
  int32_t radius = 0;             // the corners the alpha plane was cut for
  uint16_t budget = kAutoCells;   // auto mode's cell count, shrinks under load
  uint8_t cell = 0;               // the app's own cell; 0 = auto
};

inline ShaderObj* of(framewidget::Widget* w) { return reinterpret_cast<ShaderObj*>(w); }

inline lv_obj_class_t shaderClass;

static constexpr uint32_t kFrameBudgetUs = 25000;  // auto mode coarsens past this

inline void freeCells(ShaderObj* s) {
  if (s->cells) { lv_free(s->cells); s->cells = nullptr; }
  s->cellsW = s->cellsH = s->cellNow = 0;
}

inline void constructor(const lv_obj_class_t*, lv_obj_t* obj) {
  ShaderObj* s = reinterpret_cast<ShaderObj*>(obj);
  new (&s->palette) Palette();
  namedPalette("mono", s->palette);
  s->cells = nullptr;
  s->cellsW = s->cellsH = s->cellNow = 0;
  s->speed = 1;
  s->scale = 1;
  s->radius = 0;
  s->budget = kAutoCells;
  s->cell = 0;
  framewidget::construct(&s->base, "Shader");
}

inline void destructor(const lv_obj_class_t*, lv_obj_t* obj) {
  ShaderObj* s = reinterpret_cast<ShaderObj*>(obj);
  freeCells(s);
  framewidget::destroy(&s->base);
}

// Size the cells and the pixel buffer to the widget, keeping them when
// nothing changed (the common case: every frame). The cell edge holds while
// no frame is owed (cellForDraw), so every strip of one refresh draws one
// grid. A rounded widget (radius > 0) draws RGB565A8: the colour plane, then
// an alpha plane at half its stride holding the corners.
inline bool prepare(framewidget::Widget* w, int width, int height, bool& fresh) {
  ShaderObj* s = of(w);
  const int32_t radius = lv_obj_get_style_radius(&w->obj, LV_PART_MAIN);
  const bool sameSize = w->buf && (int)w->buf->header.w == width && (int)w->buf->header.h == height;
  const int c = cellForDraw(w->gate.due, sameSize, s->cellNow, width, height, s->cell, s->budget);
  const lv_color_format_t cf = radius > 0 ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
  if (!framewidget::ensureBuf(w, width, height, cf, fresh)) { freeCells(s); return false; }

  if (fresh || !s->cells || s->cellNow != c) {
    freeCells(s);
    const int cw = (width + c - 1) / c, ch = (height + c - 1) / c;
    s->cells = static_cast<uint16_t*>(lv_malloc((size_t)cw * ch * sizeof(uint16_t)));
    if (!s->cells) { framewidget::freeBuf(w); return false; }
    std::memset(s->cells, 0, (size_t)cw * ch * sizeof(uint16_t));
    s->cellsW = cw;
    s->cellsH = ch;
    s->cellNow = c;
    fresh = true;
  }

  if (radius > 0 && (fresh || radius != s->radius)) {
    cornerMask(width, height, radius, w->buf->data + (size_t)w->buf->header.stride * height,
               w->buf->header.stride / 2);
    lv_image_cache_drop(w->buf);   // same pointer, new corners
  }
  s->radius = radius;
  return true;
}

// The cell loop, run under one lua_pcall per frame (one protected call, not
// one per cell): fn(x, y, t, u, v) for every cell, one number back through
// the palette or three (r, g, b, 0..1) straight to colour.
struct Pass { ShaderObj* s; float t; Frame f; };

inline int runCells(lua_State* L) {
  Pass* p = static_cast<Pass*>(lua_touserdata(L, 1));
  ShaderObj* s = p->s;
  lua_rawgeti(L, LUA_REGISTRYINDEX, s->base.fnRef);
  const int fn = lua_gettop(L);
  for (int cy = 0; cy < s->cellsH; cy++) {
    const float y = p->f.y0 + cy * p->f.yStep, v = p->f.v0 + cy * p->f.vStep;
    uint16_t* row = s->cells + (size_t)cy * s->cellsW;
    for (int cx = 0; cx < s->cellsW; cx++) {
      lua_pushvalue(L, fn);
      lua_pushnumber(L, p->f.x0 + cx * p->f.xStep);
      lua_pushnumber(L, y);
      lua_pushnumber(L, p->t);
      lua_pushnumber(L, p->f.u0 + cx * p->f.uStep);
      lua_pushnumber(L, v);
      lua_call(L, 5, 3);
      if (lua_type(L, -2) == LUA_TNUMBER) {
        row[cx] = rgb565(unit8((float)lua_tonumber(L, -3)),
                         unit8((float)lua_tonumber(L, -2)),
                         unit8((float)lua_tonumber(L, -1)));
      } else if (lua_type(L, -3) == LUA_TNUMBER) {
        row[cx] = s->palette.lut[unit8((float)lua_tonumber(L, -3))];
      } else {
        row[cx] = 0;
      }
      lua_pop(L, 3);
    }
  }
  return 0;
}

inline void compute(ShaderObj* s, int w, int h) {
  lua_State* L = s->base.L;
  Pass p{s, framewidget::seconds(&s->base) * s->speed, frameFor(w, h, s->cellNow, s->scale)};
  const uint32_t start = micros();
  lua_pushcfunction(L, runCells);
  lua_pushlightuserdata(L, &p);
  if (lua_pcall(L, 1, 0, 0) != LUA_OK) { framewidget::reportError(&s->base, L); return; }
  // Auto mode: a frame that ran long coarsens the grid for the next, so a
  // heavy fn finds its own resolution on the hardware it runs on.
  if (s->cell == 0 && micros() - start > kFrameBudgetUs && s->cellNow < kMaxCell) {
    s->budget = (uint16_t)(s->cellsW * s->cellsH * 2 / 3);
  }
}

// The owed frame: the cells, then each cell a block — a row of the buffer
// built once per cell row, copied down the block.
inline void paint(framewidget::Widget* w, int width, int height) {
  ShaderObj* s = of(w);
  if (framewidget::live(w)) compute(s, width, height);
  const int c = s->cellNow;
  const uint32_t stride = w->buf->header.stride;
  for (int cy = 0; cy < s->cellsH; cy++) {
    const int y0 = cy * c;
    if (y0 >= height) break;
    uint16_t* row = (uint16_t*)(w->buf->data + (size_t)y0 * stride);
    const uint16_t* src = s->cells + (size_t)cy * s->cellsW;
    for (int x = 0; x < width; x++) row[x] = src[x / c];
    for (int y = y0 + 1; y < y0 + c && y < height; y++) {
      std::memcpy(w->buf->data + (size_t)y * stride, row, (size_t)width * 2);
    }
  }
}

inline const framewidget::Kind kKind{prepare, paint};

inline void eventCb(const lv_obj_class_t*, lv_event_t* e) { framewidget::handleEvent(&shaderClass, e, kKind); }

inline lv_obj_t* create(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_class_create_obj(&shaderClass, parent);
  lv_obj_class_init_obj(obj);
  return obj;
}

// ── the widget's keys ──────────────────────────────────────────────────────

// palette = "ember" | { "#000000", "#ff8800", ... } (2..8 colours)
inline void setPalette(lua_State* L, ShaderObj* s) {
  if (lua_type(L, -1) == LUA_TSTRING) {
    const char* name = lua_tostring(L, -1);
    if (!namedPalette(name, s->palette)) {
      luaL_error(L, "Shader: no palette '%s' (ember, ocean, aurora, sunset, rainbow, mono, "
                    "or a list of 2..8 colours)", name);
    }
    return;
  }
  if (!lua_istable(L, -1)) luaL_error(L, "Shader: palette is a name or a list of colours");
  const int n = (int)lua_rawlen(L, -1);
  if (n < 2 || n > kMaxStops) luaL_error(L, "Shader: a palette has 2..8 colours, not %d", n);
  Rgb stops[kMaxStops];
  for (int i = 0; i < n; i++) {
    lua_rawgeti(L, -1, i + 1);
    const lv_color_t c = luavgl_tocolor(L, -1);
    lua_pop(L, 1);
    stops[i] = {c.red, c.green, c.blue};
  }
  buildPalette(stops, n, s->palette);
}

// One key of a set{} table: the shader's own, or the base object's.
inline int setKv(lua_State* L, void* data) {
  ShaderObj* s = static_cast<ShaderObj*>(data);
  const char* k = lua_tostring(L, -2);
  if (std::strcmp(k, "fn") == 0) {
    framewidget::setFn(&s->base, L);
    s->budget = kAutoCells;   // a new fn gets a fresh look at its cost
  } else if (std::strcmp(k, "palette") == 0) {
    setPalette(L, s);
  } else if (std::strcmp(k, "speed") == 0) {
    s->speed = (float)luaL_checknumber(L, -1);
  } else if (std::strcmp(k, "scale") == 0) {
    const float v = (float)luaL_checknumber(L, -1);
    if (v <= 0) luaL_error(L, "Shader: scale must be above 0");
    s->scale = v;
  } else if (std::strcmp(k, "cell") == 0) {
    const lua_Integer v = luaL_checkinteger(L, -1);
    s->cell = (uint8_t)(v < 0 ? 0 : v > kMaxCell ? kMaxCell : v);
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

// ── the `shader` helpers ───────────────────────────────────────────────────

inline float num(lua_State* L, int i) { return (float)luaL_checknumber(L, i); }
inline float opt(lua_State* L, int i, float d) { return (float)luaL_optnumber(L, i, d); }
inline int ret(lua_State* L, float v) { lua_pushnumber(L, v); return 1; }

inline int l_fract(lua_State* L) { return ret(L, fn::fract(num(L, 1))); }
inline int l_mix(lua_State* L) { return ret(L, fn::mix(num(L, 1), num(L, 2), num(L, 3))); }
inline int l_clamp(lua_State* L) { return ret(L, fn::clamp(num(L, 1), opt(L, 2, 0), opt(L, 3, 1))); }
inline int l_step(lua_State* L) { return ret(L, fn::step(num(L, 1), num(L, 2))); }
inline int l_smoothstep(lua_State* L) { return ret(L, fn::smoothstep(num(L, 1), num(L, 2), num(L, 3))); }
inline int l_mod(lua_State* L) { return ret(L, fn::mod(num(L, 1), num(L, 2))); }
inline int l_length(lua_State* L) { return ret(L, fn::length(num(L, 1), num(L, 2))); }
inline int l_hash(lua_State* L) { return ret(L, fn::hash(num(L, 1), num(L, 2))); }
inline int l_noise(lua_State* L) { return ret(L, fn::noise(num(L, 1), num(L, 2))); }
inline int l_fbm(lua_State* L) { return ret(L, fn::fbm(num(L, 1), num(L, 2), (int)luaL_optinteger(L, 3, 4))); }
inline int l_circle(lua_State* L) { return ret(L, fn::circle(num(L, 1), num(L, 2), num(L, 3))); }
inline int l_box(lua_State* L) { return ret(L, fn::box(num(L, 1), num(L, 2), num(L, 3), num(L, 4))); }
inline int l_segment(lua_State* L) {
  return ret(L, fn::segment(num(L, 1), num(L, 2), num(L, 3), num(L, 4), num(L, 5), num(L, 6)));
}
inline int l_smin(lua_State* L) { return ret(L, fn::smin(num(L, 1), num(L, 2), num(L, 3))); }
inline int l_hsv(lua_State* L) {
  float r, g, b;
  fn::hsv(num(L, 1), opt(L, 2, 1), opt(L, 3, 1), r, g, b);
  lua_pushnumber(L, r); lua_pushnumber(L, g); lua_pushnumber(L, b);
  return 3;
}
inline int l_rot(lua_State* L) {
  float x, y;
  fn::rot(num(L, 1), num(L, 2), num(L, 3), x, y);
  lua_pushnumber(L, x); lua_pushnumber(L, y);
  return 2;
}

inline const luaL_Reg kHelpers[] = {
    {"fract", l_fract}, {"mix", l_mix}, {"clamp", l_clamp}, {"step", l_step},
    {"smoothstep", l_smoothstep}, {"mod", l_mod}, {"length", l_length},
    {"hash", l_hash}, {"noise", l_noise}, {"fbm", l_fbm}, {"hsv", l_hsv},
    {"circle", l_circle}, {"box", l_box}, {"segment", l_segment}, {"smin", l_smin},
    {"rot", l_rot}, {nullptr, nullptr},
};

}  // namespace detail

// The board's installer: lvglModule.addWidget(Resident::shader::installWidget).
inline void installWidget(lua_State* L) {
  static bool classReady = false;
  if (!classReady) {
    // A backdrop by default: it fills its parent.
    framewidget::defineClass(detail::shaderClass, "shader", sizeof(detail::ShaderObj),
                             detail::constructor, detail::destructor, detail::eventCb);
    classReady = true;
  }
  luavgl_obj_newmetatable(L, &detail::shaderClass, "lv_shader", detail::kMethods);
  lua_pop(L, 1);
  luaL_getmetatable(L, "widgets");
  lua_pushcfunction(L, detail::luaCreate);
  lua_setfield(L, -2, "Shader");
  lua_pop(L, 1);
  // The helpers, as the global `shader`: valid wherever lvgl.* is — in
  // builders, updaters and the shader function itself.
  luaL_newlib(L, detail::kHelpers);
  lua_setglobal(L, "shader");
}

}  // namespace shader
}  // namespace Resident

#endif  // __has_include(<lvgl.h>) && __has_include(<luavgl.h>)
#endif  // RESIDENT_SHADER_WIDGET_H
