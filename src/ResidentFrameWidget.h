// src/ResidentFrameWidget.h — the base of the lvgl widgets whose pixels the
// app's own Lua function makes (ResidentShaderWidget.h, ResidentLgfxWidget.h).
// It owns what they share: the function's registry ref, the fps timer, the
// owed frame (FrameGate), the pixel buffer, error reporting and the draw. A
// kind supplies two hooks (Kind) and its own keys.
//
// A frame is made in LV_EVENT_DRAW_MAIN, not in the timer, so a widget LVGL
// does not redraw — off screen, under an overlay, on a display stood down —
// costs nothing. The timer only owes a frame and invalidates, at `fps`. A
// frame is made at most once per owed frame: LvglModule renders in partial
// mode (draw buffers a tenth of the panel by default), so LVGL draws a widget
// once per strip it crosses, and every strip after the first redraws the same
// buffer. One refresh shows one moment, whatever the widget's size.
//
// A kind's object starts with a Widget, whose first member is the lv_obj_t
// (LVGL's subclassing contract):
//
//   struct ShaderObj { framewidget::Widget base; ... };
//
// and its class's callbacks call construct / destroy / handleEvent.
//
// fn's lua_State is the state's main thread, never the caller's: a key can be
// set from a coroutine, which the collector frees once it is done with, and
// the main thread shares the registry and lives as long as the state. fn runs
// outside the sandbox's execution deadline, as luavgl's own callbacks do.
//
// On a build without LVGL/luavgl this header compiles to nothing, as
// ResidentLvglModule.h does.
#ifndef RESIDENT_FRAME_WIDGET_H
#define RESIDENT_FRAME_WIDGET_H

#if __has_include(<lvgl.h>) && __has_include(<luavgl.h>)

#include <cstring>

// Lua headers carry C linkage but no C++ guards (see ResidentLvglModule.h).
extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}
#include <lvgl.h>
#include "src/lvgl_private.h"   // lv_obj_t and lv_obj_class_t, to subclass
#include <luavgl.h>

#include "ResidentFrameWidgetCore.h"

namespace Resident {
namespace framewidget {

struct Widget {
  lv_obj_t obj;                   // first: LVGL's subclassing contract
  const char* name = nullptr;     // the kind's Lua name, in its error messages
  lua_State* L = nullptr;         // the main thread of the state that owns fnRef
  int fnRef = LUA_NOREF;
  lv_draw_buf_t* buf = nullptr;   // the widget's pixels
  lv_timer_t* timer = nullptr;
  uint32_t t0 = 0;
  FrameGate gate{};
  uint8_t fps = kDefaultFps;
  bool broken = false;            // fn raised: hold the last frame until a new fn
};

struct Kind {
  // Size the kind's own buffers, and buf (through ensureBuf), to w x h.
  // Sets fresh when the pixels are new and blank. False: nothing to draw.
  bool (*prepare)(Widget* w, int width, int height, bool& fresh);
  // A frame is owed: call fn (when live()) and fill buf.
  void (*paint)(Widget* w, int width, int height);
};

// A frame is owed: make it at the next draw.
inline void owe(Widget* w) {
  w->gate.owe();
  lv_obj_invalidate(&w->obj);
}

namespace detail {

inline void onTick(lv_timer_t* t) { owe(static_cast<Widget*>(lv_timer_get_user_data(t))); }

inline void stopTimer(Widget* w) {
  if (w->timer) { lv_timer_delete(w->timer); w->timer = nullptr; }
}

inline void restartTimer(Widget* w) {
  stopTimer(w);
  if (w->fps > 0) w->timer = lv_timer_create(onTick, 1000 / w->fps, w);
  owe(w);
}

inline lua_State* mainThread(lua_State* L) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
  lua_State* main = lua_tothread(L, -1);
  lua_pop(L, 1);
  return main;
}

}  // namespace detail

// The class: a plain lv_obj subclass that fills its parent unless sized.
inline void defineClass(lv_obj_class_t& cls, const char* name, uint32_t instanceSize,
                        void (*ctor)(const lv_obj_class_t*, lv_obj_t*),
                        void (*dtor)(const lv_obj_class_t*, lv_obj_t*),
                        void (*event)(const lv_obj_class_t*, lv_event_t*)) {
  std::memset(&cls, 0, sizeof(cls));
  cls.base_class = &lv_obj_class;
  cls.constructor_cb = ctor;
  cls.destructor_cb = dtor;
  cls.event_cb = event;
  cls.name = name;
  cls.width_def = LV_PCT(100);
  cls.height_def = LV_PCT(100);
  cls.instance_size = instanceSize;
}

// From the class's constructor, after the kind's own fields: starts the
// timer and owes the first frame.
inline void construct(Widget* w, const char* name) {
  w->name = name;
  w->L = nullptr;
  w->fnRef = LUA_NOREF;
  w->buf = nullptr;
  w->timer = nullptr;
  w->t0 = lv_tick_get();
  w->gate = FrameGate{};
  w->fps = kDefaultFps;
  w->broken = false;
  lv_obj_remove_flag(&w->obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(&w->obj, LV_OBJ_FLAG_CLICKABLE);
  detail::restartTimer(w);
}

inline void freeBuf(Widget* w) {
  if (w->buf) { lv_image_cache_drop(w->buf); lv_draw_buf_destroy(w->buf); w->buf = nullptr; }
}

// From the class's destructor, after the kind's own teardown.
inline void destroy(Widget* w) {
  detail::stopTimer(w);
  if (w->L && w->fnRef != LUA_NOREF) luaL_unref(w->L, LUA_REGISTRYINDEX, w->fnRef);
  w->fnRef = LUA_NOREF;
  freeBuf(w);
}

// fps = v (clamped 0..kMaxFps; 0 = only when owed by a key).
inline void setFps(Widget* w, lua_Integer v) {
  w->fps = clampFps((long)v);
  if (!w->broken) detail::restartTimer(w);
}

// fn = the value at -1: a function, or nil for none. Unbreaks the widget.
inline void setFn(Widget* w, lua_State* L) {
  if (!lua_isnil(L, -1)) luaL_checktype(L, -1, LUA_TFUNCTION);
  if (w->L && w->fnRef != LUA_NOREF) luaL_unref(w->L, LUA_REGISTRYINDEX, w->fnRef);
  w->fnRef = LUA_NOREF;
  if (!lua_isnil(L, -1)) {
    lua_pushvalue(L, -1);
    w->fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  w->L = detail::mainThread(L);
  if (w->broken) { w->broken = false; detail::restartTimer(w); }
}

// Has a function, and it has not raised.
inline bool live(const Widget* w) { return w->L && w->fnRef != LUA_NOREF && !w->broken; }

// Seconds since the widget was made.
inline float seconds(const Widget* w) { return lv_tick_elaps(w->t0) / 1000.0f; }

// buf at w x h in cf, kept when it already is; fresh when it was remade.
inline bool ensureBuf(Widget* w, int width, int height, lv_color_format_t cf, bool& fresh) {
  fresh = false;
  if (w->buf && w->buf->header.cf == cf &&
      (int)w->buf->header.w == width && (int)w->buf->header.h == height) return true;
  freeBuf(w);
  w->buf = lv_draw_buf_create(width, height, cf, LV_STRIDE_AUTO);
  fresh = w->buf != nullptr;
  return fresh;
}

// fn raised, message at -1 (popped): say so where the app's errors go
// (log.error, which reaches the serial log and the app's error telemetry),
// and stop calling it until it is given a new fn.
inline void reportError(Widget* w, lua_State* L) {
  const int top = lua_gettop(L) - 1;   // below the message
  const char* msg = lua_tostring(L, -1);
  lua_getglobal(L, "log");
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "error");
    if (lua_isfunction(L, -1)) {
      lua_pushfstring(L, "%s: %s (stopped until it is given a new fn)", w->name,
                      msg ? msg : "error");
      lua_pcall(L, 1, 0, 0);
    }
  }
  lua_settop(L, top);
  w->broken = true;
  detail::stopTimer(w);
}

// From the class's event callback.
inline void handleEvent(const lv_obj_class_t* cls, lv_event_t* e, const Kind& kind) {
  if (lv_obj_event_base(cls, e) != LV_RESULT_OK) return;
  if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
  Widget* w = static_cast<Widget*>(lv_event_get_current_target(e));

  lv_area_t a;
  lv_obj_get_coords(&w->obj, &a);
  const int width = lv_area_get_width(&a), height = lv_area_get_height(&a);
  if (width <= 0 || height <= 0) return;
  bool fresh = false;
  if (!kind.prepare(w, width, height, fresh)) return;

  if (w->gate.take(fresh)) {
    kind.paint(w, width, height);
    lv_image_cache_drop(w->buf);   // same pointer, new pixels
  }

  lv_draw_image_dsc_t dsc;
  lv_draw_image_dsc_init(&dsc);
  dsc.src = w->buf;
  dsc.opa = lv_obj_get_style_opa_recursive(&w->obj, LV_PART_MAIN);
  lv_draw_image(lv_event_get_layer(e), &dsc, &a);
}

// The widget's set{...}: kv takes each key, its own or the base object's.
inline int set(lua_State* L, int (*kv)(lua_State* L, void* obj)) {
  lv_obj_t* obj = luavgl_to_obj(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  luavgl_iterate(L, 2, kv, obj);
  lua_settop(L, 1);
  return 1;
}

}  // namespace framewidget
}  // namespace Resident

#endif  // __has_include(<lvgl.h>) && __has_include(<luavgl.h>)
#endif  // RESIDENT_FRAME_WIDGET_H
