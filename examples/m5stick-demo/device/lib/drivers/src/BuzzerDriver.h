#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

#include <cstdint>
#include <OutrunDriver.h>
#include <OutrunLuaModule.h>

// Outrun driver for buzzer via M5Unified Speaker API.
// Lua API: buzzer.beep(freq_hz, duration_ms)
//          buzzer.tone(freq_hz)  — continuous until stop
//          buzzer.stop()
class BuzzerDriver : public Outrun::Driver {
public:
  explicit BuzzerDriver(uint8_t volume = 128) : _volume(volume) {}

  const char* name() const override { return "buzzer"; }
  void begin() override;
  void onAppReset() override;
  void registerModule(Outrun::LuaModule& m) override {
    m.method<BuzzerDriver, &BuzzerDriver::beep>("beep")
     .method<BuzzerDriver, &BuzzerDriver::tone>("tone")
     .method<BuzzerDriver, &BuzzerDriver::stop>("stop");
  }

  int beep(lua_State* L);
  int tone(lua_State* L);
  int stop(lua_State* L);

private:
  uint8_t _volume;
};

#endif // BUZZER_DRIVER_H
