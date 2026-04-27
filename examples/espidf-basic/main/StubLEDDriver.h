#pragma once

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>

// Minimal no-op driver. Exercises the new Resident::Extension lifecycle
// without requiring real hardware. Exposes an `led` table in Lua with one
// function: led.set(r, g, b)  -- accepts but ignores three integers.
class StubLEDDriver : public Resident::Driver {
public:
    const char* name() const override { return "led"; }
    void registerModule(Resident::LuaModule& m) override {
        m.method<StubLEDDriver, &StubLEDDriver::set>("set");
    }

    int set(lua_State* L);
};
