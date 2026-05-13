#ifndef BATTERY_DRIVER_H
#define BATTERY_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <Adafruit_LC709203F.h>

// Resident driver wrapping the onboard LC709203F fuel gauge. Exposes the
// `battery.*` Lua module. The LC709203F is powered by VBAT — when no LiPo
// is plugged into the JST connector, the chip is invisible on I2C and the
// `present` flag is false. In that case the read accessors return 0.
class BatteryDriver : public Resident::Driver {
public:
  BatteryDriver(Adafruit_LC709203F* lc, const bool* presentFlag)
      : _lc(lc), _present(presentFlag) {}

  const char* name() const override { return "battery"; }

  void registerModule(Resident::LuaModule& m) override {
    m.method<BatteryDriver, &BatteryDriver::voltage>("voltage")
     .method<BatteryDriver, &BatteryDriver::percent>("percent")
     .method<BatteryDriver, &BatteryDriver::present>("present");
  }

private:
  Adafruit_LC709203F* _lc;
  const bool* _present;

  int voltage(lua_State* L);
  int percent(lua_State* L);
  int present(lua_State* L);
};

#endif // BATTERY_DRIVER_H
