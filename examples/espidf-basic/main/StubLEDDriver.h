#pragma once

#include <OutrunDriver.h>

// Minimal no-op driver. Exercises Sandbox::addDriver() without requiring
// real hardware. Exposes an `led` table in Lua with one function:
//   led.set(r, g, b)  -- accepts but ignores three integers
class StubLEDDriver : public Outrun::Driver {
public:
    const char* name() const override { return "led"; }
    void installSandboxModule(lua_State* L) override;
};
