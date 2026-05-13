#include "LEDDriver.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

// led.set(r, g, b) — set the onboard NeoPixel colour (0-255 per channel).
int LEDDriver::set(lua_State* L) {
  int r = (int)luaL_checknumber(L, 1);
  int g = (int)luaL_checknumber(L, 2);
  int b = (int)luaL_checknumber(L, 3);
  if (r < 0) r = 0; else if (r > 255) r = 255;
  if (g < 0) g = 0; else if (g > 255) g = 255;
  if (b < 0) b = 0; else if (b > 255) b = 255;
  _pixel->setPixelColor(0, _pixel->Color(r, g, b));
  _pixel->show();
  return 0;
}

// led.set_brightness(0-255) — global brightness applied on next show().
int LEDDriver::setBrightness(lua_State* L) {
  int b = (int)luaL_checknumber(L, 1);
  if (b < 0) b = 0; else if (b > 255) b = 255;
  _pixel->setBrightness(b);
  _pixel->show();
  return 0;
}

int LEDDriver::off(lua_State* L) {
  (void)L;
  _pixel->setPixelColor(0, 0);
  _pixel->show();
  return 0;
}
