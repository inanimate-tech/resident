#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <ResidentStatusLED.h>
#include <Adafruit_NeoPixel.h>

// A StatusLED (which is a Driver): exposes the `led.*` Lua module and drives
// the onboard NeoPixel for connection state (yellow=connecting,
// cyan=transports, green=connected, orange=reconnecting, red=failed) until a
// Lua app loads. solidColor() no-ops once an app is running so the app fully
// owns the pixel. The Adafruit_NeoPixel must already be begin()'d in
// main.cpp before Sandbox::setup() runs.
class LEDDriver : public Resident::StatusLED {
public:
  explicit LEDDriver(Adafruit_NeoPixel* pixel) : _pixel(pixel) {}

  const char* name() const override { return "led"; }

  void registerModule(Resident::LuaModule& m) override {
    m.method<LEDDriver, &LEDDriver::set>("set")
     .method<LEDDriver, &LEDDriver::setBrightness>("set_brightness")
     .method<LEDDriver, &LEDDriver::off>("off");
  }

  // Sandbox lifecycle: clear on app reset; track app-running so the
  // StatusLED stops touching the pixel once Lua takes over.
  void onAppReset() override {
    _pixel->setPixelColor(0, 0);
    _pixel->show();
  }
  void onAppRunning(bool running) override { _appRunning = running; }

  // Resident::StatusLED — Resident calls this during the connection
  // lifecycle. Suppressed once a Lua app owns the pixel.
  void solidColor(uint32_t color) override;

private:
  Adafruit_NeoPixel* _pixel;
  bool _appRunning = false;

  int set(lua_State* L);
  int setBrightness(lua_State* L);
  int off(lua_State* L);
};

#endif // LED_DRIVER_H
