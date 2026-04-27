#include <unity.h>
extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

void setUp(void) {}
void tearDown(void) {}

void test_can_create_lua_state(void) {
    lua_State* L = luaL_newstate();
    TEST_ASSERT_NOT_NULL(L);
    luaL_openlibs(L);
    int rc = luaL_dostring(L, "return 1 + 1");
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(2, (int)lua_tointeger(L, -1));
    lua_close(L);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_can_create_lua_state);
    return UNITY_END();
}
