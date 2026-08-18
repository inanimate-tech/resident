#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <ResidentStatusDisplay.h>
#include <ResidentRenderTargets.h>
#include <M5Unified.h>

// A StatusDisplay (which is a Driver): renders Lua screen.* via an off-screen
// sprite and presents it through the board's one PanelTarget (R19). Status
// display calls are suppressed while an app is running.
//
// This is the system/status display ROLE, deliberately outside the R19
// bind-is-the-claim arbitration: connection text and the status sprite must
// reach the glass whether or not an app has claimed the panel for a
// graphics library.
class DisplayDriver : public Resident::StatusDisplay {
public:
  explicit DisplayDriver(Resident::PanelTarget* panel) : _panel(panel) {}

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
#ifdef HAS_QRCODE
    m.method<DisplayDriver, &DisplayDriver::qr>("qr");
#endif
  }

  void onAppReset() override;
  void onAppRunning(bool running) override { _appRunning = running; }

  // Resident::StatusDisplay — writes directly to M5.Display (not sprite)
  void displayText(const char* text) override;

  // Call once after M5.begin() to create the sprite framebuffer
  void begin() override;

  // Re-push the current off-screen sprite to the display without redrawing it.
  // Restores the last app frame after a direct displayText() overlay (e.g. the
  // m5stick-voice "Listening" prompt). Essential for apps that render only in
  // init(); tick-driven apps would otherwise redraw on their next on_tick.
  void repaint();

  // The off-screen sprite, for sharing with the lgfx module: wrap it in an
  // LgfxSpriteTarget and both drawing surfaces (screen.* verbs and lgfx) hit
  // the same framebuffer, presented through the same panel.
  M5Canvas& canvas() { return _canvas; }

protected:
  M5Canvas _canvas{&M5.Display};

private:
  Resident::PanelTarget* _panel = nullptr;
  bool _spriteReady = false;
  bool _appRunning = false;

  // One present path for the sprite: the board's panel.
  void push();

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
