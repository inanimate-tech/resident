// src/OutrunLuaModule.h
#ifndef OUTRUN_LUA_MODULE_H
#define OUTRUN_LUA_MODULE_H

extern "C" {
  #include "lua/lua.h"
}

namespace Outrun {

// MemberFnClass<&C::m>::type -> C
template<class T> struct MemberFnClass;
template<class C, class R, class... A>
struct MemberFnClass<R (C::*)(A...)> { using type = C; };
template<class C, class R, class... A>
struct MemberFnClass<R (C::*)(A...) const> { using type = C; };

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
template<auto MemberFn>
struct Trampoline {
  using ClassT = typename MemberFnClass<decltype(MemberFn)>::type;
  static int call(lua_State* L) {
    void* ud = lua_touserdata(L, lua_upvalueindex(1));
    ClassT* self = static_cast<ClassT*>(ud);
    return (self->*MemberFn)(L);
  }
};

// Builder. Caller leaves a fresh table on top of the Lua stack before
// constructing; populate with method/staticMethod/constant; caller
// `lua_setglobal`s the table afterwards.
class LuaModule {
public:
  LuaModule(lua_State* L, void* self) : _L(L), _self(self) {}

  template<auto MemberFn>
  LuaModule& method(const char* name) {
    lua_pushlightuserdata(_L, _self);
    lua_pushcclosure(_L, &Trampoline<MemberFn>::call, 1);
    lua_setfield(_L, -2, name);
    return *this;
  }

  LuaModule& staticMethod(const char* name, lua_CFunction fn) {
    lua_pushcfunction(_L, fn);
    lua_setfield(_L, -2, name);
    return *this;
  }

  LuaModule& constant(const char* name, int value) {
    lua_pushinteger(_L, value);
    lua_setfield(_L, -2, name);
    return *this;
  }
  LuaModule& constant(const char* name, double value) {
    lua_pushnumber(_L, value);
    lua_setfield(_L, -2, name);
    return *this;
  }
  LuaModule& constant(const char* name, const char* value) {
    lua_pushstring(_L, value);
    lua_setfield(_L, -2, name);
    return *this;
  }
  LuaModule& constant(const char* name, bool value) {
    lua_pushboolean(_L, value);
    lua_setfield(_L, -2, name);
    return *this;
  }

private:
  lua_State* _L;
  void* _self;
};

} // namespace Outrun

#endif // OUTRUN_LUA_MODULE_H
