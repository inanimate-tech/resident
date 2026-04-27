#include <unity.h>
extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}
#include "OutrunLuaModule.h"

namespace {

class Calc {
public:
  int value = 0;
  int add(lua_State* L) {
    int n = (int)luaL_checkinteger(L, 1);
    value += n;
    lua_pushinteger(L, value);
    return 1;
  }
  int reset(lua_State* L) {
    (void)L;
    value = 0;
    return 0;
  }
};

}

static lua_State* L = nullptr;

void setUp(void) {
  L = luaL_newstate();
  luaL_openlibs(L);
}
void tearDown(void) {
  lua_close(L);
}

void test_method_binds_and_recovers_this(void) {
    Calc calc;
    lua_newtable(L);
    Outrun::LuaModule m(L, &calc);
    m.method<&Calc::add>("add");
    lua_setglobal(L, "calc");

    int rc = luaL_dostring(L, "return calc.add(7)");
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(7, (int)lua_tointeger(L, -1));
    TEST_ASSERT_EQUAL_INT(7, calc.value);
    lua_pop(L, 1);
}

void test_method_chain(void) {
    Calc calc;
    lua_newtable(L);
    Outrun::LuaModule(L, &calc)
      .method<&Calc::add>("add")
      .method<&Calc::reset>("reset");
    lua_setglobal(L, "calc");

    int rc = luaL_dostring(L, "calc.add(3); calc.add(4); calc.reset(); return calc.add(1)");
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, (int)lua_tointeger(L, -1));
    lua_pop(L, 1);
}

void test_static_method(void) {
    auto fn = +[](lua_State* L2) -> int {
      lua_pushinteger(L2, 42);
      return 1;
    };
    lua_newtable(L);
    Outrun::LuaModule(L, nullptr).staticMethod("answer", fn);
    lua_setglobal(L, "m");

    int rc = luaL_dostring(L, "return m.answer()");
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(42, (int)lua_tointeger(L, -1));
    lua_pop(L, 1);
}

void test_constants(void) {
    lua_newtable(L);
    Outrun::LuaModule(L, nullptr)
      .constant("ANSWER", 42)
      .constant("PI", 3.14)
      .constant("NAME", "outrun")
      .constant("ON", true);
    lua_setglobal(L, "k");

    int rc = luaL_dostring(L, "return k.ANSWER, k.PI, k.NAME, k.ON");
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(42,    (int)lua_tointeger(L, -4));
    TEST_ASSERT_EQUAL_FLOAT(3.14, (float)lua_tonumber(L, -3));
    TEST_ASSERT_EQUAL_STRING("outrun", lua_tostring(L, -2));
    TEST_ASSERT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 4);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_method_binds_and_recovers_this);
    RUN_TEST(test_method_chain);
    RUN_TEST(test_static_method);
    RUN_TEST(test_constants);
    return UNITY_END();
}
