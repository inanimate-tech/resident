// Smoke test — proves the native test environment compiles and runs.
// Real unit tests for resident source land in sibling test_*/ directories.

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_smoke(void) {
    TEST_ASSERT_TRUE(true);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
