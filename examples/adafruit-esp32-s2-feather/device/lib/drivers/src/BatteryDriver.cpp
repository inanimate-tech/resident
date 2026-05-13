#include "BatteryDriver.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

int BatteryDriver::voltage(lua_State* L) {
  if (!*_present) {
    lua_pushnumber(L, 0.0);
  } else {
    lua_pushnumber(L, _lc->cellVoltage());
  }
  return 1;
}

int BatteryDriver::percent(lua_State* L) {
  if (!*_present) {
    lua_pushnumber(L, 0.0);
  } else {
    lua_pushnumber(L, _lc->cellPercent());
  }
  return 1;
}

int BatteryDriver::present(lua_State* L) {
  lua_pushboolean(L, *_present);
  return 1;
}
