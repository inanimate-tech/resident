#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <OutrunDriver.h>
#include <OutrunStatusDisplay.h>
#include <M5Unified.h>

// Outrun driver wrapping M5.Display for Lua access.
// Dual role: Outrun::Driver (Lua screen.* module via sprite buffer)
//          + Outrun::StatusDisplay (connection state text, direct to display)
// When an app is running, status display calls are suppressed.
class DisplayDriver : public Outrun::Driver, public Outrun::StatusDisplay {
public:
  const char* name() const override { return "screen"; }
  void installSandboxModule(lua_State* L) override;
  void onAppReset() override;
  void onAppRunning(bool running) override { _appRunning = running; }

  // Outrun::StatusDisplay — writes directly to M5.Display (not sprite)
  void displayText(const char* text) override;

  // Call once after M5.begin() to create the sprite framebuffer
  void begin();

protected:
  M5Canvas _canvas{&M5.Display};

private:
  bool _initialized = false;
  bool _appRunning = false;

  static DisplayDriver* getFromLua(lua_State* L, const char* fn);

  static int lua_display_clear(lua_State* L);
  static int lua_display_text(lua_State* L);
  static int lua_display_fill_rect(lua_State* L);
  static int lua_display_rect(lua_State* L);
  static int lua_display_line(lua_State* L);
  static int lua_display_triangle(lua_State* L);
  static int lua_display_fill_triangle(lua_State* L);
  static int lua_display_pixel(lua_State* L);
  static int lua_display_flip(lua_State* L);
  static int lua_display_set_brightness(lua_State* L);
  static int lua_display_width(lua_State* L);
  static int lua_display_height(lua_State* L);
#ifdef HAS_QRCODE
  static int lua_display_qr(lua_State* L);
#endif
};

#endif // DISPLAY_DRIVER_H
