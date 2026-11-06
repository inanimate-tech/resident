#ifndef RESIDENT_APP_ENV_H
#define RESIDENT_APP_ENV_H

// The app's environment: the table an app's chunk runs in as its _ENV, and
// so where every global it reads or writes lives.
//
// The sandbox builds a fresh one at every app load (Sandbox::buildAppEnv)
// from the global table as it stood when the sandbox finished setting up:
// each library and module table is copied, so `string.format = f` or
// `pairs = f` changes only the app that did it. The global table itself
// stays pristine; the framework environment and the datetime and module
// chunks read it, so nothing an app does steers them.
//
// So a driver or widget that gives the app a global must put it in the
// app's environment, not in _G with lua_setglobal: the app would never see
// a global set there. These helpers do that from any lua_State of the
// sandbox (a coroutine's included), by way of the registry. They read and
// write raw: the app may give its environment a metatable, and a driver's
// update() must not run app code outside the execution guard.

extern "C" {
#include "lua/lua.h"
}

namespace Resident {

// Where the running app's environment is kept: LUA_REGISTRYINDEX[key].
inline constexpr const char* kAppEnvRegistryKey = "resident.app_env";

// Push the running app's environment, or nil before the first app load.
// Returns the pushed value's type (LUA_TTABLE or LUA_TNIL).
inline int pushAppEnv(lua_State* L) {
  return lua_getfield(L, LUA_REGISTRYINDEX, kAppEnvRegistryKey);
}

// env[name] = the value on top of the stack, which is popped either way.
// Returns false when there is no app environment yet: the next app load
// builds one from the pristine globals, so a global an app needs is put
// back for each app (from a hook that runs for each app, such as a widget
// installer on bind).
inline bool setAppGlobal(lua_State* L, const char* name) {
  if (pushAppEnv(L) != LUA_TTABLE) {
    lua_pop(L, 2);  // nil, value
    return false;
  }
  lua_insert(L, -2);  // [env, value]
  lua_pushstring(L, name);
  lua_insert(L, -2);  // [env, name, value]
  lua_rawset(L, -3);
  lua_pop(L, 1);  // env
  return true;
}

// Push env[name], or nil when there is no app environment. Returns the
// pushed value's type.
inline int getAppGlobal(lua_State* L, const char* name) {
  if (pushAppEnv(L) != LUA_TTABLE) return LUA_TNIL;  // the nil stays pushed
  lua_pushstring(L, name);
  const int t = lua_rawget(L, -2);
  lua_remove(L, -2);  // env
  return t;
}

}  // namespace Resident

#endif  // RESIDENT_APP_ENV_H
