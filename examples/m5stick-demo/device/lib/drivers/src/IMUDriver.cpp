#include "IMUDriver.h"
#include <M5Unified.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

// Body-frame convention used by all imu.* Lua bindings:
//   body-X = long axis of the device (USB port toward -X)
//   body-Y = short axis
//   body-Z = screen normal (positive points out of the screen toward the viewer)
// At rest face-up on a table, accel should read approximately (0, 0, +1g).
// Gyro uses the right-hand rule: thumb along the +axis, positive rotation is
// CCW when looking toward the tip of the thumb.
//
// M5Unified reports values in the chip's own frame. On the M5StickC Plus2 the
// MPU6886 is physically mounted rotated so its X and Y axes are swapped relative
// to the body frame described above; we un-swap here so Lua apps get consistent
// body-frame readings. Other boards (M5StickS3 etc.) pass through unchanged;
// verify and extend this remap when adding a new board.
static inline void remapToBodyFrame(float& x, float& y) {
  if (M5.getBoard() == m5::board_t::board_M5StickCPlus2) {
    float tmp = x;
    x = y;
    y = tmp;
  }
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

// imu.accel() -> ax, ay, az (g-force, body frame)
int IMUDriver::lua_imu_accel(lua_State* L) {
  M5.Imu.update();
  auto data = M5.Imu.getImuData();
  float ax = data.accel.x, ay = data.accel.y, az = data.accel.z;
  remapToBodyFrame(ax, ay);
  lua_pushnumber(L, ax);
  lua_pushnumber(L, ay);
  lua_pushnumber(L, az);
  return 3;
}

// imu.gyro() -> gx, gy, gz (degrees/sec, body frame)
int IMUDriver::lua_imu_gyro(lua_State* L) {
  M5.Imu.update();
  auto data = M5.Imu.getImuData();
  float gx = data.gyro.x, gy = data.gyro.y, gz = data.gyro.z;
  remapToBodyFrame(gx, gy);
  lua_pushnumber(L, gx);
  lua_pushnumber(L, gy);
  lua_pushnumber(L, gz);
  return 3;
}

// imu.temp() -> not supported by M5Unified IMU_Class, returns 0
int IMUDriver::lua_imu_temp(lua_State* L) {
  lua_pushnumber(L, 0);
  return 1;
}
