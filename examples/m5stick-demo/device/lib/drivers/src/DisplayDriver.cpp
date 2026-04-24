#include "DisplayDriver.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

#ifdef HAS_QRCODE
#include "qrcode.h"
#endif

void DisplayDriver::begin() {
  // setColorDepth must come BEFORE createSprite — the sprite buffer is
  // allocated at whatever depth is set when create is called.
  _canvas.setColorDepth(16);
  _canvas.createSprite(M5.Display.width(), M5.Display.height());
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

  lua_pushcfunction(L, lua_display_rect);
  lua_setfield(L, -2, "rect");

  lua_pushcfunction(L, lua_display_line);
  lua_setfield(L, -2, "line");

  lua_pushcfunction(L, lua_display_triangle);
  lua_setfield(L, -2, "triangle");

  lua_pushcfunction(L, lua_display_fill_triangle);
  lua_setfield(L, -2, "fill_triangle");

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

#ifdef HAS_QRCODE
  lua_pushcfunction(L, lua_display_qr);
  lua_setfield(L, -2, "qr");
#endif

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

// screen.clear(r, g, b) — fill sprite with color (0-255 per channel)
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

// screen.text(x, y, str, [size], [r, g, b]) — draw text at position in sprite.
// Defaults: size=2, colour=white. Any of size/r/g/b can be omitted from the end.
int DisplayDriver::lua_display_text(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.text");
  if (!d) return 0;

  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  const char* str = luaL_checkstring(L, 3);
  int size = (int)luaL_optinteger(L, 4, 2);
  int r = (int)luaL_optinteger(L, 5, 255);
  int g = (int)luaL_optinteger(L, 6, 255);
  int b = (int)luaL_optinteger(L, 7, 255);

  d->_canvas.setCursor(x, y);
  d->_canvas.setTextColor(d->_canvas.color565(r, g, b));
  d->_canvas.setTextSize(size);
  d->_canvas.print(str);
  return 0;
}

// screen.fill_rect(x, y, w, h, r, g, b) — filled rectangle in sprite
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

// screen.rect(x, y, w, h, r, g, b) — 1px outline rectangle in sprite
int DisplayDriver::lua_display_rect(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.rect");
  if (!d) return 0;

  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);

  uint16_t color = d->_canvas.color565(r, g, b);
  d->_canvas.drawRect(x, y, w, h, color);
  return 0;
}

// screen.line(x0, y0, x1, y1, r, g, b) — 1px line in sprite
int DisplayDriver::lua_display_line(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.line");
  if (!d) return 0;

  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);

  uint16_t color = d->_canvas.color565(r, g, b);
  d->_canvas.drawLine(x0, y0, x1, y1, color);
  return 0;
}

// screen.triangle(x0, y0, x1, y1, x2, y2, r, g, b) — 1px outline triangle
int DisplayDriver::lua_display_triangle(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.triangle");
  if (!d) return 0;

  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int x2 = (int)luaL_checknumber(L, 5);
  int y2 = (int)luaL_checknumber(L, 6);
  int r = (int)luaL_checknumber(L, 7);
  int g = (int)luaL_checknumber(L, 8);
  int b = (int)luaL_checknumber(L, 9);

  uint16_t color = d->_canvas.color565(r, g, b);
  d->_canvas.drawTriangle(x0, y0, x1, y1, x2, y2, color);
  return 0;
}

// screen.fill_triangle(x0, y0, x1, y1, x2, y2, r, g, b) — filled triangle
int DisplayDriver::lua_display_fill_triangle(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.fill_triangle");
  if (!d) return 0;

  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int x2 = (int)luaL_checknumber(L, 5);
  int y2 = (int)luaL_checknumber(L, 6);
  int r = (int)luaL_checknumber(L, 7);
  int g = (int)luaL_checknumber(L, 8);
  int b = (int)luaL_checknumber(L, 9);

  uint16_t color = d->_canvas.color565(r, g, b);
  d->_canvas.fillTriangle(x0, y0, x1, y1, x2, y2, color);
  return 0;
}

// screen.pixel(x, y, r, g, b) — set single pixel in sprite
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

// screen.flip() — push sprite framebuffer to display (single SPI transfer).
int DisplayDriver::lua_display_flip(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.flip");
  if (!d) return 0;

  d->_canvas.pushSprite(0, 0);
  return 0;
}

// screen.set_brightness(0-255)
int DisplayDriver::lua_display_set_brightness(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.set_brightness");
  if (!d) return 0;

  int brightness = (int)luaL_checknumber(L, 1);
  if (brightness < 0) brightness = 0;
  if (brightness > 255) brightness = 255;
  M5.Display.setBrightness(brightness);
  return 0;
}

// screen.width() — returns display width in pixels
int DisplayDriver::lua_display_width(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.width");
  if (!d) { lua_pushinteger(L, 0); return 1; }
  lua_pushinteger(L, M5.Display.width());
  return 1;
}

// screen.height() — returns display height in pixels
int DisplayDriver::lua_display_height(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.height");
  if (!d) { lua_pushinteger(L, 0); return 1; }
  lua_pushinteger(L, M5.Display.height());
  return 1;
}

#ifdef HAS_QRCODE
// screen.qr(x, y, str, [scale], [r, g, b]) — draw QR code in sprite.
// Auto-picks the smallest QR version (3..10, ECC low) that fits the text.
// Defaults: scale=4, colour=black. Background is not drawn — caller is
// responsible for clearing to a light colour behind the QR for scannability.
int DisplayDriver::lua_display_qr(lua_State* L) {
  DisplayDriver* d = getFromLua(L, "screen.qr");
  if (!d) return 0;

  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  const char* str = luaL_checkstring(L, 3);
  int scale = (int)luaL_optinteger(L, 4, 4);
  int r = (int)luaL_optinteger(L, 5, 0);
  int g = (int)luaL_optinteger(L, 6, 0);
  int b = (int)luaL_optinteger(L, 7, 0);
  if (scale < 1) scale = 1;

  // qrcode_getBufferSize is a runtime fn, not a macro — hardcode a size
  // generous for version 10 (actual requirement ~407 bytes).
  static constexpr uint8_t QR_MAX_VERSION = 10;
  static uint8_t qr_buffer[512];
  QRCode qrcode;
  int8_t result = -1;
  for (uint8_t version = 3; version <= QR_MAX_VERSION; version++) {
    result = qrcode_initText(&qrcode, qr_buffer, version, ECC_LOW, str);
    if (result == 0) break;
  }
  if (result != 0) {
    return luaL_error(L, "screen.qr: text too long for QR v%d", QR_MAX_VERSION);
  }

  uint16_t color = d->_canvas.color565(r, g, b);
  for (uint8_t py = 0; py < qrcode.size; py++) {
    for (uint8_t px = 0; px < qrcode.size; px++) {
      if (qrcode_getModule(&qrcode, px, py)) {
        d->_canvas.fillRect(x + px * scale, y + py * scale, scale, scale, color);
      }
    }
  }
  return 0;
}
#endif
