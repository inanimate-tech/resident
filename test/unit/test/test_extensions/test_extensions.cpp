#include <unity.h>
#include "OutrunExtension.h"
#include "OutrunExtensions.h"

namespace {
class StubExt : public Outrun::Extension {
public:
  int id;
  explicit StubExt(int i) : id(i) {}
  const char* name() const override { return "stub"; }
};
}

void setUp(void) {}
void tearDown(void) {}

void test_default_is_empty(void) {
    Outrun::Extensions e;
    TEST_ASSERT_EQUAL_INT(0, (int)e.count);
}

void test_initializer_list_assignment_copies_pointers(void) {
    StubExt a(1), b(2), c(3);
    Outrun::Extensions e;
    e = {&a, &b, &c};
    TEST_ASSERT_EQUAL_INT(3, (int)e.count);
    TEST_ASSERT_EQUAL_PTR(&a, e.items[0]);
    TEST_ASSERT_EQUAL_PTR(&b, e.items[1]);
    TEST_ASSERT_EQUAL_PTR(&c, e.items[2]);
}

void test_oversize_list_clamps_to_max(void) {
    StubExt s[12] = {StubExt(0),StubExt(1),StubExt(2),StubExt(3),
                     StubExt(4),StubExt(5),StubExt(6),StubExt(7),
                     StubExt(8),StubExt(9),StubExt(10),StubExt(11)};
    Outrun::Extensions e;
    e = {&s[0],&s[1],&s[2],&s[3],&s[4],&s[5],&s[6],&s[7],
         &s[8],&s[9],&s[10],&s[11]};
    TEST_ASSERT_EQUAL_INT(Outrun::Extensions::MAX, (int)e.count);
    TEST_ASSERT_EQUAL_PTR(&s[0], e.items[0]);
    TEST_ASSERT_EQUAL_PTR(&s[Outrun::Extensions::MAX - 1],
                          e.items[Outrun::Extensions::MAX - 1]);
}

void test_reassignment_resets_all_slots(void) {
    // Production pattern: makeConfig() builds DeviceConfig fresh each call,
    // but tests/setup may reassign Extensions on an existing struct. Verify
    // that assigning a shorter list nulls out the previously-set tail slots
    // — this depends on `items[MAX] = {}` in the struct definition keeping
    // unset slots null in the temporary constructed from the brace list.
    StubExt a(1), b(2), c(3);
    Outrun::Extensions e;
    e = {&a, &b};
    e = {&c};
    TEST_ASSERT_EQUAL_INT(1, (int)e.count);
    TEST_ASSERT_EQUAL_PTR(&c, e.items[0]);
    TEST_ASSERT_NULL(e.items[1]);   // must be null, not stale &b
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_is_empty);
    RUN_TEST(test_initializer_list_assignment_copies_pointers);
    RUN_TEST(test_oversize_list_clamps_to_max);
    RUN_TEST(test_reassignment_resets_all_slots);
    return UNITY_END();
}
