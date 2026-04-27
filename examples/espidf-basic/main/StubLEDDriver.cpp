#include "StubLEDDriver.h"

extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
}

namespace {
int l_set(lua_State* L) {
    luaL_checkinteger(L, 1);
    luaL_checkinteger(L, 2);
    luaL_checkinteger(L, 3);
    return 0;
}

const luaL_Reg led_funcs[] = {
    {"set", l_set},
    {nullptr, nullptr},
};
} // namespace

void StubLEDDriver::installSandboxModule(lua_State* L) {
    luaL_newlib(L, led_funcs);
    lua_setglobal(L, "led");
}
