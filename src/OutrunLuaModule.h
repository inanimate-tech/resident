// src/OutrunLuaModule.h
#ifndef OUTRUN_LUA_MODULE_H
#define OUTRUN_LUA_MODULE_H

extern "C" {
  #include "lua/lua.h"
}

#include <type_traits>

namespace Outrun {

class Extension;  // forward decl for the Trampoline static_assert

// Trampoline: read this from upvalue 1, call member fn with lua_State*.
//
// The void* upvalue is the Extension* registered with Sandbox. Casting
// void* -> ClassT* is correct only when Extension is the leftmost base of
// ClassT, so the addresses are numerically equal. This is the case for
// every single-inheritance driver. For dual-inheritance drivers (e.g. one
// that is also an Outrun::StatusDisplay), declare Driver first:
//
//   class Foo : public Outrun::Driver, public Outrun::StatusDisplay  // OK
//   class Foo : public Outrun::StatusDisplay, public Outrun::Driver  // BROKEN
template<class C, int (C::*F)(lua_State*)>
struct Trampoline {
  static_assert(std::is_base_of<Extension, C>::value,
                "LuaModule::method<> requires the member function's class "
                "to derive from Outrun::Extension.");
  static int call(lua_State* L) {
    void* ud = lua_touserdata(L, lua_upvalueindex(1));
    return (static_cast<C*>(ud)->*F)(L);
  }
};

// Const-member-fn overload (e.g. read-only display dimension accessors).
template<class C, int (C::*F)(lua_State*) const>
struct TrampolineConst {
  static_assert(std::is_base_of<Extension, C>::value,
                "LuaModule::method<> requires the member function's class "
                "to derive from Outrun::Extension.");
  static int call(lua_State* L) {
    void* ud = lua_touserdata(L, lua_upvalueindex(1));
    return (static_cast<const C*>(ud)->*F)(L);
  }
};

// Builder. Caller leaves a fresh table on top of the Lua stack before
// constructing; populate with method/staticMethod/constant; caller
// `lua_setglobal`s the table afterwards.
class LuaModule {
public:
  LuaModule(lua_State* L, void* self) : _lua(L), _self(self) {}

  template<class C, int (C::*F)(lua_State*)>
  LuaModule& method(const char* name) {
    lua_pushlightuserdata(_lua, _self);
    lua_pushcclosure(_lua, &Trampoline<C, F>::call, 1);
    lua_setfield(_lua, -2, name);
    return *this;
  }

  template<class C, int (C::*F)(lua_State*) const>
  LuaModule& method(const char* name) {
    lua_pushlightuserdata(_lua, _self);
    lua_pushcclosure(_lua, &TrampolineConst<C, F>::call, 1);
    lua_setfield(_lua, -2, name);
    return *this;
  }

  LuaModule& staticMethod(const char* name, lua_CFunction fn) {
    lua_pushcfunction(_lua, fn);
    lua_setfield(_lua, -2, name);
    return *this;
  }

  LuaModule& constant(const char* name, int value) {
    lua_pushinteger(_lua, value);
    lua_setfield(_lua, -2, name);
    return *this;
  }
  LuaModule& constant(const char* name, double value) {
    lua_pushnumber(_lua, value);
    lua_setfield(_lua, -2, name);
    return *this;
  }
  LuaModule& constant(const char* name, const char* value) {
    lua_pushstring(_lua, value);
    lua_setfield(_lua, -2, name);
    return *this;
  }
  LuaModule& constant(const char* name, bool value) {
    lua_pushboolean(_lua, value);
    lua_setfield(_lua, -2, name);
    return *this;
  }

private:
  lua_State* _lua;
  void* _self;
};

} // namespace Outrun

#endif // OUTRUN_LUA_MODULE_H
