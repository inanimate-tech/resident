#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

#include <cstdint>
#include <OutrunDriver.h>

// Outrun driver for buzzer via M5Unified Speaker API.
// Lua API: buzzer.beep(freq_hz, duration_ms)
//          buzzer.tone(freq_hz)  — continuous until stop
//          buzzer.stop()
class BuzzerDriver : public Outrun::Driver {
public:
  explicit BuzzerDriver(uint8_t volume = 128) : _volume(volume) {}

  void begin();
  const char* name() const override { return "buzzer"; }
  void installSandboxModule(lua_State* L) override;
  void onAppReset() override;

private:
  uint8_t _volume;

  static int lua_buzzer_beep(lua_State* L);
  static int lua_buzzer_tone(lua_State* L);
  static int lua_buzzer_stop(lua_State* L);
};

#endif // BUZZER_DRIVER_H
