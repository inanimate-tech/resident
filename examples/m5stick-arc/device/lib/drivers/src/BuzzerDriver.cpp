#include "BuzzerDriver.h"
#include <M5Unified.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

void BuzzerDriver::begin() {
  M5.Speaker.setVolume(_volume);
}

void BuzzerDriver::onAppReset() {
  M5.Speaker.stop();
}

// buzzer.beep(freq_hz, duration_ms)
int BuzzerDriver::beep(lua_State* L) {
  int freq = (int)luaL_checkinteger(L, 1);
  int duration = (int)luaL_checkinteger(L, 2);
  if (freq < 20) freq = 20;
  if (freq > 20000) freq = 20000;
  if (duration < 0) duration = 0;
  if (duration > 5000) duration = 5000;
  M5.Speaker.tone(freq, duration);
  return 0;
}

// buzzer.tone(freq_hz) — continuous tone until stop()
int BuzzerDriver::tone(lua_State* L) {
  int freq = (int)luaL_checkinteger(L, 1);
  if (freq < 20) freq = 20;
  if (freq > 20000) freq = 20000;
  M5.Speaker.tone(freq);
  return 0;
}

// buzzer.stop()
int BuzzerDriver::stop(lua_State* L) {
  (void)L;
  M5.Speaker.stop();
  return 0;
}
