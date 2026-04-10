#ifndef IMU_DRIVER_H
#define IMU_DRIVER_H

#include <OutrunDriver.h>

// Outrun driver wrapping M5Unified's IMU (MPU6886) for Lua access.
// Lua API: imu.accel() -> ax,ay,az (g-force)
//          imu.gyro()  -> gx,gy,gz (degrees/sec)
//          imu.temp()  -> celsius
class IMUDriver : public Outrun::Driver {
public:
  const char* name() const override { return "imu"; }
  void installSandboxModule(lua_State* L) override;

private:
  static int lua_imu_accel(lua_State* L);
  static int lua_imu_gyro(lua_State* L);
  static int lua_imu_temp(lua_State* L);
};

#endif // IMU_DRIVER_H
