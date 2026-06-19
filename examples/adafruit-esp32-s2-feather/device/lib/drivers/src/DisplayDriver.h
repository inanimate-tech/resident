#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <ResidentStatusDisplay.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// A StatusDisplay (which is a Driver): renders Lua screen.* via an off-screen
// GFXcanvas16 and writes connection-state text straight to the TFT, suppressed
// while a Lua app is running. Owns the PWM channel for TFT_BACKLITE so
// screen.set_brightness can control it.
class DisplayDriver : public Resident::StatusDisplay {
public:
  DisplayDriver(Adafruit_ST7789* tft, uint8_t backlitePin)
      : _tft(tft), _backlitePin(backlitePin) {}

  const char* name() const override { return "screen"; }

  void registerModule(Resident::LuaModule& m) override {
    m.method<DisplayDriver, &DisplayDriver::clear>("clear")
     .method<DisplayDriver, &DisplayDriver::text>("text")
     .method<DisplayDriver, &DisplayDriver::fillRect>("fill_rect")
     .method<DisplayDriver, &DisplayDriver::rect>("rect")
     .method<DisplayDriver, &DisplayDriver::line>("line")
     .method<DisplayDriver, &DisplayDriver::triangle>("triangle")
     .method<DisplayDriver, &DisplayDriver::fillTriangle>("fill_triangle")
     .method<DisplayDriver, &DisplayDriver::pixel>("pixel")
     .method<DisplayDriver, &DisplayDriver::flip>("flip")
     .method<DisplayDriver, &DisplayDriver::setBrightness>("set_brightness")
     .method<DisplayDriver, &DisplayDriver::width>("width")
     .method<DisplayDriver, &DisplayDriver::height>("height");
  }

  void onAppReset() override;
  void onAppRunning(bool running) override { _appRunning = running; }

  // Resident::StatusDisplay — direct draw (not via canvas)
  void displayText(const char* text) override;

  // Called once by Device::setup(). Allocates the canvas and prepares the
  // backlight PWM channel. The Adafruit_ST7789 itself must already be
  // `init()`'d (and rotated) by the time this runs — main.cpp does that
  // before handing off to Resident.
  void begin() override;

private:
  Adafruit_ST7789* _tft;
  uint8_t _backlitePin;
  GFXcanvas16* _canvas = nullptr;
  bool _spriteReady = false;
  bool _appRunning = false;

  int clear(lua_State* L);
  int text(lua_State* L);
  int fillRect(lua_State* L);
  int rect(lua_State* L);
  int line(lua_State* L);
  int triangle(lua_State* L);
  int fillTriangle(lua_State* L);
  int pixel(lua_State* L);
  int flip(lua_State* L);
  int setBrightness(lua_State* L);
  int width(lua_State* L);
  int height(lua_State* L);
};

#endif // DISPLAY_DRIVER_H
