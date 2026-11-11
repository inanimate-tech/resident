#ifndef RESIDENT_APP_BASE_LIB_H
#define RESIDENT_APP_BASE_LIB_H

// The app's base library: the Lua base functions an app reaches, narrowed
// where the stock version lets one app reach past its own dispatch. Every
// such wrapper lives here, and install() is the one place they replace the
// stock functions in _G (Sandbox::setupLuaEnvironment, before the baseline
// snapshot). The framework environment and the datetime module read _G
// too, so they get the same functions an app does.
//
// - setmetatable refuses a metatable that carries __gc. A finalizer runs
//   inside the garbage collector, at whatever allocation triggers a step or
//   at app reset, with hooks off (lgc.c: GCTM clears allowhook), so the
//   execution deadline cannot stop it, and an error in it is only a
//   warning. One `while true do end` there hangs the VM for good, and a
//   persisted app re-arms it on every boot. Whether an object has a
//   finalizer is decided once, when the metatable is attached, from its raw
//   __gc (luaC_checkfinalizer), so checking the raw field here is the whole
//   check for tables: adding __gc afterwards registers nothing. GCTM reads
//   __gc again when the finalizer runs, which is why the userdata rule
//   below matters.
// - getmetatable returns nil for userdata. A binding's userdata metatables
//   are shared by every object of the type and are usually not locked, so
//   `getmetatable(obj).__gc = f` would redirect the native finalizer of
//   every such object to f.
// - collectgarbage takes only the options that leave the collector as it
//   was: "collect" (the default), "count", "step" and "isrunning". "stop",
//   "restart", "setpause", "setstepmul", "incremental" and "generational"
//   change the collector for the whole VM, so for every app after this one.
//
// openUnsafeLibs does not reopen the stock versions: they are not unsafe
// libraries, and a board that opens `debug` has debug.setmetatable anyway.

#include <string.h>

extern "C" {
#include "lua/lauxlib.h"
#include "lua/lua.h"
}

namespace Resident {
namespace appbase {

// setmetatable(t, mt): the stock function, refusing an mt with a raw __gc.
inline int setmetatable(lua_State* L) {
  const int t = lua_type(L, 2);
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_argexpected(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table");
  if (luaL_getmetafield(L, 1, "__metatable") != LUA_TNIL)
    return luaL_error(L, "cannot change a protected metatable");
  if (t == LUA_TTABLE) {
    lua_pushliteral(L, "__gc");
    const bool hasGc = lua_rawget(L, 2) != LUA_TNIL;
    lua_pop(L, 1);
    if (hasGc) return luaL_error(L, "setmetatable: __gc is not available to apps");
  }
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;
}

// getmetatable(v): the stock function, except nil for any userdata.
inline int getmetatable(lua_State* L) {
  luaL_checkany(L, 1);
  const int t = lua_type(L, 1);
  if (t == LUA_TUSERDATA || t == LUA_TLIGHTUSERDATA || !lua_getmetatable(L, 1)) {
    lua_pushnil(L);
    return 1;
  }
  luaL_getmetafield(L, 1, "__metatable");  // pushes __metatable, if set, over the metatable
  return 1;
}

// collectgarbage([opt [, arg]]): the stock function's read-only and
// one-off options; any other raises.
inline int collectgarbage(lua_State* L) {
  static const char* const kOpts[] = {"collect", "count", "step", "isrunning", nullptr};
  const char* name = luaL_optstring(L, 1, "collect");
  int opt = 0;
  while (kOpts[opt] && strcmp(kOpts[opt], name) != 0) opt++;
  if (!kOpts[opt])
    return luaL_error(L, "collectgarbage: option '%s' is not available to apps", name);
  int res;
  switch (opt) {
    case 0:  // collect
      res = lua_gc(L, LUA_GCCOLLECT);
      if (res == -1) break;
      lua_pushinteger(L, res);
      return 1;
    case 1: {  // count: KB, fractional
      res = lua_gc(L, LUA_GCCOUNT);
      const int bytes = lua_gc(L, LUA_GCCOUNTB);
      if (res == -1) break;
      lua_pushnumber(L, (lua_Number)res + ((lua_Number)bytes / 1024));
      return 1;
    }
    case 2:  // step
      res = lua_gc(L, LUA_GCSTEP, (int)luaL_optinteger(L, 2, 0));
      if (res == -1) break;
      lua_pushboolean(L, res);
      return 1;
    default:  // isrunning
      res = lua_gc(L, LUA_GCISRUNNING);
      if (res == -1) break;
      lua_pushboolean(L, res);
      return 1;
  }
  luaL_pushfail(L);  // inside a finalizer, as the stock function answers
  return 1;
}

// Replace the stock functions in _G with the narrowed ones.
inline void install(lua_State* L) {
  lua_pushcfunction(L, setmetatable);
  lua_setglobal(L, "setmetatable");
  lua_pushcfunction(L, getmetatable);
  lua_setglobal(L, "getmetatable");
  lua_pushcfunction(L, collectgarbage);
  lua_setglobal(L, "collectgarbage");
}

}  // namespace appbase
}  // namespace Resident

#endif  // RESIDENT_APP_BASE_LIB_H
