#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <Adafruit_NeoPixel.h>

// Resident driver for the single onboard NeoPixel. Exposes the `led.*` Lua
// module. The Adafruit_NeoPixel must already be `begin()`'d in main.cpp
// before Resident::Device::setup() runs.
class LEDDriver : public Resident::Driver {
public:
  explicit LEDDriver(Adafruit_NeoPixel* pixel) : _pixel(pixel) {}

  const char* name() const override { return "led"; }

  void registerModule(Resident::LuaModule& m) override {
    m.method<LEDDriver, &LEDDriver::set>("set")
     .method<LEDDriver, &LEDDriver::setBrightness>("set_brightness")
     .method<LEDDriver, &LEDDriver::off>("off");
  }

  // On app reset, clear the pixel so a misbehaving app doesn't leave it on.
  void onAppReset() override {
    _pixel->setPixelColor(0, 0);
    _pixel->show();
  }

private:
  Adafruit_NeoPixel* _pixel;

  int set(lua_State* L);
  int setBrightness(lua_State* L);
  int off(lua_State* L);
};

#endif // LED_DRIVER_H
