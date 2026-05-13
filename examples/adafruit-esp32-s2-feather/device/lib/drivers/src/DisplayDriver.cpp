#include "DisplayDriver.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

static constexpr uint8_t BACKLIGHT_LEDC_CH = 0;

static constexpr uint16_t toRGB565(int r, int g, int b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}

void DisplayDriver::begin() {
  if (_initialized) return;  // dual-inherit (Driver + StatusDisplay) → guard
  // Canvas matches the TFT's post-rotation logical size (set in main.cpp
  // before handing off to Resident, so width()/height() reflect rotation).
  _canvas = new GFXcanvas16(_tft->width(), _tft->height());
  // Backlight on PWM channel — claim a LEDC channel and start at full bright.
  ledcSetup(BACKLIGHT_LEDC_CH, 5000, 8);  // 5 kHz, 8-bit
  ledcAttachPin(_backlitePin, BACKLIGHT_LEDC_CH);
  ledcWrite(BACKLIGHT_LEDC_CH, 255);
  _initialized = true;
}

void DisplayDriver::displayText(const char* text) {
  if (_appRunning) return;
  _tft->fillScreen(ST77XX_BLACK);
  _tft->setTextWrap(false);

  _tft->setTextColor(ST77XX_CYAN);
  _tft->setTextSize(2);
  _tft->setCursor(5, 5);
  _tft->print("Resident");

  _tft->setTextColor(ST77XX_WHITE);
  _tft->setTextSize(1);
  _tft->setCursor(5, 30);
  _tft->print("ESP32-S2 TFT Feather");

  bool looksLikeId = (text != nullptr && strlen(text) == 8);
  _tft->setTextColor(looksLikeId ? ST77XX_GREEN : ST77XX_YELLOW);
  _tft->setTextSize(2);  // size 3 × 8 chars = 144 px, overflows the 135 wide portrait
  _tft->setCursor(5, 80);
  _tft->print(text ? text : "");
}

void DisplayDriver::onAppReset() {
  if (_canvas) {
    _canvas->fillScreen(0);
    _tft->drawRGBBitmap(0, 0, _canvas->getBuffer(), _canvas->width(), _canvas->height());
  } else {
    _tft->fillScreen(ST77XX_BLACK);
  }
}

// screen.clear([r, g, b]) — fill canvas with colour (defaults to black)
int DisplayDriver::clear(lua_State* L) {
  int r = (int)luaL_optinteger(L, 1, 0);
  int g = (int)luaL_optinteger(L, 2, 0);
  int b = (int)luaL_optinteger(L, 3, 0);
  _canvas->fillScreen(toRGB565(r, g, b));
  return 0;
}

// screen.text(x, y, str, [size=2], [r=255, g=255, b=255])
int DisplayDriver::text(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  const char* str = luaL_checkstring(L, 3);
  int size = (int)luaL_optinteger(L, 4, 2);
  int r = (int)luaL_optinteger(L, 5, 255);
  int g = (int)luaL_optinteger(L, 6, 255);
  int b = (int)luaL_optinteger(L, 7, 255);
  _canvas->setCursor(x, y);
  _canvas->setTextColor(toRGB565(r, g, b));
  _canvas->setTextSize(size);
  _canvas->setTextWrap(false);
  _canvas->print(str);
  return 0;
}

int DisplayDriver::fillRect(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);
  _canvas->fillRect(x, y, w, h, toRGB565(r, g, b));
  return 0;
}

int DisplayDriver::rect(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int w = (int)luaL_checknumber(L, 3);
  int h = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);
  _canvas->drawRect(x, y, w, h, toRGB565(r, g, b));
  return 0;
}

int DisplayDriver::line(lua_State* L) {
  int x0 = (int)luaL_checknumber(L, 1);
  int y0 = (int)luaL_checknumber(L, 2);
  int x1 = (int)luaL_checknumber(L, 3);
  int y1 = (int)luaL_checknumber(L, 4);
  int r = (int)luaL_checknumber(L, 5);
  int g = (int)luaL_checknumber(L, 6);
  int b = (int)luaL_checknumber(L, 7);
  _canvas->drawLine(x0, y0, x1, y1, toRGB565(r, g, b));
  return 0;
}

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
  _canvas->drawTriangle(x0, y0, x1, y1, x2, y2, toRGB565(r, g, b));
  return 0;
}

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
  _canvas->fillTriangle(x0, y0, x1, y1, x2, y2, toRGB565(r, g, b));
  return 0;
}

int DisplayDriver::pixel(lua_State* L) {
  int x = (int)luaL_checknumber(L, 1);
  int y = (int)luaL_checknumber(L, 2);
  int r = (int)luaL_checknumber(L, 3);
  int g = (int)luaL_checknumber(L, 4);
  int b = (int)luaL_checknumber(L, 5);
  _canvas->drawPixel(x, y, toRGB565(r, g, b));
  return 0;
}

// screen.flip() — push the canvas to the TFT in a single SPI transfer.
int DisplayDriver::flip(lua_State* L) {
  (void)L;
  _tft->drawRGBBitmap(0, 0, _canvas->getBuffer(), _canvas->width(), _canvas->height());
  return 0;
}

// screen.set_brightness(0-255) — PWM the TFT backlight.
int DisplayDriver::setBrightness(lua_State* L) {
  int b = (int)luaL_checknumber(L, 1);
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  ledcWrite(BACKLIGHT_LEDC_CH, b);
  return 0;
}

int DisplayDriver::width(lua_State* L) {
  lua_pushinteger(L, _tft->width());
  return 1;
}

int DisplayDriver::height(lua_State* L) {
  lua_pushinteger(L, _tft->height());
  return 1;
}
