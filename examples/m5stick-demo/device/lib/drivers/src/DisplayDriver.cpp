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
  if (_initialized) return;   // dual-inherit (Driver + StatusDisplay) →
                              // Device.setup() and Sandbox.initialize()
                              // both reach us; guard against re-init.
  // setColorDepth must come BEFORE createSprite — the sprite buffer is
  // allocated at whatever depth is set when create is called.
  _canvas.setColorDepth(16);
  _canvas.createSprite(M5.Display.width(), M5.Display.height());
  _initialized = true;
}

void DisplayDriver::displayText(const char* text) {
  if (_appRunning) return;

  // Render into the off-screen sprite and push it in one shot. Drawing
  // directly to M5.Display (fillScreen + print) blacks out the panel between
  // the clear and the redraw, which flickers visibly when the text updates
  // frequently — e.g. the once-a-second boot countdown.
  if (!_initialized) {              // sprite not allocated yet (pre-begin)
    M5.Display.fillScreen(TFT_BLACK);
    return;
  }
  _canvas.fillScreen(TFT_BLACK);
  _canvas.setTextColor(TFT_WHITE);
  _canvas.setTextSize(2);
  _canvas.setScrollRect(4, 60, _canvas.width() - 8, _canvas.height() - 60);
  _canvas.setTextScroll(true);
  _canvas.setCursor(4, 60);
  _canvas.print(text);
  _canvas.setTextScroll(false);    // restore default; drawn pixels unaffected
  _canvas.pushSprite(0, 0);
}

void DisplayDriver::repaint() {
  if (_initialized) _canvas.pushSprite(0, 0);
}

void DisplayDriver::onAppReset() {
  if (_initialized) {
    _canvas.fillScreen(TFT_BLACK);
    _canvas.pushSprite(0, 0);
  } else {
    M5.Display.fillScreen(TFT_BLACK);
  }
}

// screen.clear(r, g, b) — fill sprite with color (0-255 per channel)
int DisplayDriver::clear(lua_State* L) {
  int r = luaL_optinteger(L, 1, 0);
  int g = luaL_optinteger(L, 2, 0);
  int b = luaL_optinteger(L, 3, 0);

  uint16_t color = _canvas.color565(r, g, b);
  _canvas.fillScreen(color);
  return 0;
}

// screen.text(x, y, str, [size], [r, g, b]) — draw text at position in sprite.
// Defaults: size=2, colour=white. Any of size/r/g/b can be omitted from the end.
int DisplayDriver::text(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  const char* str = luaL_checkstring(L, 3);
  int size = (int)luaL_optinteger(L, 4, 2);
  int r = (int)luaL_optinteger(L, 5, 255);
  int g = (int)luaL_optinteger(L, 6, 255);
  int b = (int)luaL_optinteger(L, 7, 255);

  _canvas.setCursor(x, y);
  _canvas.setTextColor(_canvas.color565(r, g, b));
  _canvas.setTextSize(size);
  _canvas.print(str);
  return 0;
}

// screen.fill_rect(x, y, w, h, r, g, b) — filled rectangle in sprite
int DisplayDriver::fillRect(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);

  uint16_t color = _canvas.color565(r, g, b);
  _canvas.fillRect(x, y, w, h, color);
  return 0;
}

// screen.rect(x, y, w, h, r, g, b) — 1px outline rectangle in sprite
int DisplayDriver::rect(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);

  uint16_t color = _canvas.color565(r, g, b);
  _canvas.drawRect(x, y, w, h, color);
  return 0;
}

// screen.line(x0, y0, x1, y1, r, g, b) — 1px line in sprite
int DisplayDriver::line(lua_State* L) {
  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);

  uint16_t color = _canvas.color565(r, g, b);
  _canvas.drawLine(x0, y0, x1, y1, color);
  return 0;
}

// screen.triangle(x0, y0, x1, y1, x2, y2, r, g, b) — 1px outline triangle
int DisplayDriver::triangle(lua_State* L) {
  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int x2 = (int)luaL_checknumber(L, 5);
  int y2 = (int)luaL_checknumber(L, 6);
  int r = (int)luaL_checknumber(L, 7);
  int g = (int)luaL_checknumber(L, 8);
  int b = (int)luaL_checknumber(L, 9);

  uint16_t color = _canvas.color565(r, g, b);
  _canvas.drawTriangle(x0, y0, x1, y1, x2, y2, color);
  return 0;
}

// screen.fill_triangle(x0, y0, x1, y1, x2, y2, r, g, b) — filled triangle
int DisplayDriver::fillTriangle(lua_State* L) {
  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int x2 = (int)luaL_checknumber(L, 5);
  int y2 = (int)luaL_checknumber(L, 6);
  int r = (int)luaL_checknumber(L, 7);
  int g = (int)luaL_checknumber(L, 8);
  int b = (int)luaL_checknumber(L, 9);

  uint16_t color = _canvas.color565(r, g, b);
  _canvas.fillTriangle(x0, y0, x1, y1, x2, y2, color);
  return 0;
}

// screen.pixel(x, y, r, g, b) — set single pixel in sprite
int DisplayDriver::pixel(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  int g = (int)luaL_checknumber(L, 4);
  int b = (int)luaL_checknumber(L, 5);

  uint16_t color = _canvas.color565(r, g, b);
  _canvas.drawPixel(x, y, color);
  return 0;
}

// screen.flip() — push sprite framebuffer to display (single SPI transfer).
int DisplayDriver::flip(lua_State* L) {
  _canvas.pushSprite(0, 0);
  return 0;
}

// screen.set_brightness(0-255)
int DisplayDriver::setBrightness(lua_State* L) {
  int brightness = (int)luaL_checknumber(L, 1);
  if (brightness < 0) brightness = 0;
  if (brightness > 255) brightness = 255;
  M5.Display.setBrightness(brightness);
  return 0;
}

// screen.width() — returns display width in pixels
int DisplayDriver::width(lua_State* L) {
  lua_pushinteger(L, M5.Display.width());
  return 1;
}

// screen.height() — returns display height in pixels
int DisplayDriver::height(lua_State* L) {
  lua_pushinteger(L, M5.Display.height());
  return 1;
}

#ifdef HAS_QRCODE
// screen.qr(x, y, str, [scale], [r, g, b]) — draw QR code in sprite.
// Auto-picks the smallest QR version (3..10, ECC low) that fits the text.
// Defaults: scale=4, colour=black. Background is not drawn — caller is
// responsible for clearing to a light colour behind the QR for scannability.
int DisplayDriver::qr(lua_State* L) {
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

  uint16_t color = _canvas.color565(r, g, b);
  for (uint8_t py = 0; py < qrcode.size; py++) {
    for (uint8_t px = 0; px < qrcode.size; px++) {
      if (qrcode_getModule(&qrcode, px, py)) {
        _canvas.fillRect(x + px * scale, y + py * scale, scale, scale, color);
      }
    }
  }
  return 0;
}
#endif
