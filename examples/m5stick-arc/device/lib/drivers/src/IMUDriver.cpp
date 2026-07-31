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
// M5Unified reports values in the chip's own frame, which differs per board:
//   M5StickC Plus2 — MPU6886 mounted rotated; X and Y are swapped.
//   M5StickS3      — IMU X axis is inverted; Y and Z match the body frame
//                    (verified on hardware: flat and landscape read correct,
//                    portrait was inverted before this remap).
// remapToBodyFrame() corrects this so Lua apps get consistent body-frame
// readings. Selection is compile-time via the per-env board macros (see
// platformio.ini / main.cpp); the S3 env's PlatformIO board is a generic
// esp32-s3-devkitc-1, so a runtime M5.getBoard() check can't identify it.
// Verify and extend this remap when adding a new board.
static inline void remapToBodyFrame(float& x, float& y) {
#if defined(BOARD_M5STICKS3)
  x = -x;
#else  // BOARD_M5STICK_C_PLUS2
  if (M5.getBoard() == m5::board_t::board_M5StickCPlus2) {
    float tmp = x;
    x = y;
    y = tmp;
  }
#endif
}

// imu.accel() -> ax, ay, az (g-force, body frame)
int IMUDriver::accel(lua_State* L) {
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
int IMUDriver::gyro(lua_State* L) {
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
int IMUDriver::temp(lua_State* L) {
  lua_pushnumber(L, 0);
  return 1;
}
