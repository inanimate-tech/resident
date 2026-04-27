#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <OutrunDriver.h>
#include <OutrunLuaModule.h>
#include <OutrunStatusDisplay.h>
#include <M5Unified.h>

// Outrun driver wrapping M5.Display for Lua access.
// Dual role: Outrun::Driver (Lua screen.* module via sprite buffer)
//          + Outrun::StatusDisplay (connection state text, direct to display)
// When an app is running, status display calls are suppressed.
class DisplayDriver : public Outrun::Driver, public Outrun::StatusDisplay {
public:
  const char* name() const override { return "screen"; }

  void registerModule(Outrun::LuaModule& m) override {
    m.method<&DisplayDriver::clear>("clear")
     .method<&DisplayDriver::text>("text")
     .method<&DisplayDriver::fillRect>("fill_rect")
     .method<&DisplayDriver::rect>("rect")
     .method<&DisplayDriver::line>("line")
     .method<&DisplayDriver::triangle>("triangle")
     .method<&DisplayDriver::fillTriangle>("fill_triangle")
     .method<&DisplayDriver::pixel>("pixel")
     .method<&DisplayDriver::flip>("flip")
     .method<&DisplayDriver::setBrightness>("set_brightness")
     .method<&DisplayDriver::width>("width")
     .method<&DisplayDriver::height>("height");
#ifdef HAS_QRCODE
    m.method<&DisplayDriver::qr>("qr");
#endif
  }

  void onAppReset() override;
  void onAppRunning(bool running) override { _appRunning = running; }

  // Outrun::StatusDisplay — writes directly to M5.Display (not sprite)
  void displayText(const char* text) override;

  // Call once after M5.begin() to create the sprite framebuffer
  void begin() override;

protected:
  M5Canvas _canvas{&M5.Display};

private:
  bool _initialized = false;
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
#ifdef HAS_QRCODE
  int qr(lua_State* L);
#endif
};

#endif // DISPLAY_DRIVER_H
