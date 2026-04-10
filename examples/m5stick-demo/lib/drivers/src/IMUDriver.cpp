#include "IMUDriver.h"
#include <M5Unified.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

void IMUDriver::installSandboxModule(lua_State* L) {
  lua_newtable(L);

  lua_pushcfunction(L, lua_imu_accel);
  lua_setfield(L, -2, "accel");

  lua_pushcfunction(L, lua_imu_gyro);
  lua_setfield(L, -2, "gyro");

  lua_pushcfunction(L, lua_imu_temp);
  lua_setfield(L, -2, "temp");

  lua_setglobal(L, "imu");
}

int IMUDriver::lua_imu_accel(lua_State* L) {
  M5.Imu.update();
  auto data = M5.Imu.getImuData();
  lua_pushnumber(L, data.accel.x);
  lua_pushnumber(L, data.accel.y);
  lua_pushnumber(L, data.accel.z);
  return 3;
}

int IMUDriver::lua_imu_gyro(lua_State* L) {
  M5.Imu.update();
  auto data = M5.Imu.getImuData();
  lua_pushnumber(L, data.gyro.x);
  lua_pushnumber(L, data.gyro.y);
  lua_pushnumber(L, data.gyro.z);
  return 3;
}

int IMUDriver::lua_imu_temp(lua_State* L) {
  lua_pushnumber(L, 0);
  return 1;
}
