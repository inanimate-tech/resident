#ifndef IMU_DRIVER_H
#define IMU_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>

// Resident driver wrapping M5Unified's IMU (MPU6886) for Lua access.
// Lua API: imu.accel() -> ax,ay,az (g-force, body frame)
//          imu.gyro()  -> gx,gy,gz (degrees/sec, body frame)
//          imu.temp()  -> not supported, returns 0
class IMUDriver : public Resident::Driver {
public:
  const char* name() const override { return "imu"; }
  void registerModule(Resident::LuaModule& m) override {
    m.method<IMUDriver, &IMUDriver::accel>("accel")
     .method<IMUDriver, &IMUDriver::gyro>("gyro")
     .method<IMUDriver, &IMUDriver::temp>("temp");
  }

  int accel(lua_State* L);
  int gyro(lua_State* L);
  int temp(lua_State* L);
};

#endif // IMU_DRIVER_H
