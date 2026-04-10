#include "DisplayDriver.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

void DisplayDriver::begin() {
  _canvas.createSprite(M5.Display.width(), M5.Display.height());
  _canvas.setColorDepth(16);
  _initialized = true;
}

void DisplayDriver::displayText(const char* text) {
  if (_appRunning) return;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 60);
  M5.Display.println(text);
}

void DisplayDriver::installSandboxModule(lua_State* L) {
  lua_pushlightuserdata(L, this);
  lua_setfield(L, LUA_REGISTRYINDEX, "DisplayDriver_instance");

  lua_newtable(L);

  lua_pushcfunction(L, lua_display_clear);
  lua_setfield(L, -2, "clear");

  lua_pushcfunction(L, lua_display_text);
  lua_setfield(L, -2, "text");

  lua_pushcfunction(L, lua_display_fill_rect);
  lua_setfield(L, -2, "fill_rect");

  lua_pushcfunction(L, lua_display_pixel);
  lua_setfield(L, -2, "pixel");

  lua_pushcfunction(L, lua_display_flip);
  lua_setfield(L, -2, "flip");

  lua_pushcfunction(L, lua_display_set_brightness);
  lua_setfield(L, -2, "set_brightness");

  lua_pushcfunction(L, lua_display_width);
  lua_setfield(L, -2, "width");

  lua_pushcfunction(L, lua_display_height);
  lua_setfield(L, -2, "height");

  lua_setglobal(L, "screen");
}

void DisplayDriver::onAppReset() {
  if (_initialized) {
    _canvas.fillScreen(TFT_BLACK);
    _canvas.pushSprite(0, 0);
  } else {
    M5.Display.fillScreen(TFT_BLACK);
  }
}

DisplayDriver* DisplayDriver::getFromLua(lua_State* L, const char* fn) {
  lua_getfield(L, LUA_REGISTRYINDEX, "DisplayDriver_instance");
  DisplayDriver* d = (DisplayDriver*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (!d) {
    luaL_error(L, "%s: screen not available", fn);
    return nullptr;
  }
  return d;
}

int DisplayDriver::lua_display_clear(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.clear");
  if (!d) return 0;
  int r = luaL_optinteger(L, 1, 0);
  int g = luaL_optinteger(L, 2, 0);
  int b = luaL_optinteger(L, 3, 0);
  uint16_t color = d->_canvas.color565(r, g, b);
  d->_canvas.fillScreen(color);
  return 0;
}

int DisplayDriver::lua_display_text(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.text");
  if (!d) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  const char* str = luaL_checkstring(L, 3);
  d->_canvas.setCursor(x, y);
  d->_canvas.setTextColor(TFT_WHITE);
  d->_canvas.setTextSize(2);
  d->_canvas.print(str);
  return 0;
}

int DisplayDriver::lua_display_fill_rect(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.fill_rect");
  if (!d) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);
  uint16_t color = d->_canvas.color565(r, g, b);
  d->_canvas.fillRect(x, y, w, h, color);
  return 0;
}

int DisplayDriver::lua_display_pixel(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.pixel");
  if (!d) return 0;
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  int g = (int)luaL_checknumber(L, 4);
  int b = (int)luaL_checknumber(L, 5);
  uint16_t color = d->_canvas.color565(r, g, b);
  d->_canvas.drawPixel(x, y, color);
  return 0;
}

int DisplayDriver::lua_display_flip(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.flip");
  if (!d) return 0;
  d->_canvas.pushSprite(0, 0);
  return 0;
}

int DisplayDriver::lua_display_set_brightness(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.set_brightness");
  if (!d) return 0;
  int brightness = (int)luaL_checknumber(L, 1);
  if (brightness < 0) brightness = 0;
  if (brightness > 255) brightness = 255;
  M5.Display.setBrightness(brightness);
  return 0;
}

int DisplayDriver::lua_display_width(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.width");
  if (!d) { lua_pushinteger(L, 0); return 1; }
  lua_pushinteger(L, M5.Display.width());
  return 1;
}

int DisplayDriver::lua_display_height(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.height");
  if (!d) { lua_pushinteger(L, 0); return 1; }
  lua_pushinteger(L, M5.Display.height());
  return 1;
}
