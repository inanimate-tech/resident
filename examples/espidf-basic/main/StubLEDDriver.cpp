#include "StubLEDDriver.h"

extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
}

int StubLEDDriver::set(lua_State* L) {
    luaL_checkinteger(L, 1);
    luaL_checkinteger(L, 2);
    luaL_checkinteger(L, 3);
    return 0;
}
