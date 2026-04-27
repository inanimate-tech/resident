#pragma once

#include <OutrunDriver.h>
#include <OutrunLuaModule.h>

// Minimal no-op driver. Exercises the new Outrun::Extension lifecycle
// without requiring real hardware. Exposes an `led` table in Lua with one
// function: led.set(r, g, b)  -- accepts but ignores three integers.
class StubLEDDriver : public Outrun::Driver {
public:
    const char* name() const override { return "led"; }
    void registerModule(Outrun::LuaModule& m) override {
        m.method<StubLEDDriver, &StubLEDDriver::set>("set");
    }

    int set(lua_State* L);
};
