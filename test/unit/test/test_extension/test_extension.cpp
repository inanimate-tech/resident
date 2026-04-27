#include <unity.h>
#include "OutrunExtension.h"

namespace {

class TestExtension : public Outrun::Extension {
public:
  int beginCount = 0;
  int updateCount = 0;
  int resetCount = 0;
  const char* name() const override { return "test"; }
  void begin() override { beginCount++; }
  void update() override { updateCount++; }
  void onAppReset() override { resetCount++; }
};

}

void setUp(void) {}
void tearDown(void) {}

void test_default_lifecycle_does_nothing(void) {
    class Bare : public Outrun::Extension {
      public: const char* name() const override { return "bare"; }
    };
    Bare b;
    b.begin();   // default no-op — must compile and not crash
    b.update();
    b.onAppReset();
    TEST_PASS();
}

void test_begin_if_needed_runs_once(void) {
    TestExtension e;
    Outrun::Extension::beginExtension(e);
    Outrun::Extension::beginExtension(e);
    Outrun::Extension::beginExtension(e);
    TEST_ASSERT_EQUAL_INT(1, e.beginCount);
}

void test_subclass_overrides_invoked(void) {
    TestExtension e;
    e.begin();
    e.update();
    e.update();
    e.onAppReset();
    TEST_ASSERT_EQUAL_INT(1, e.beginCount);
    TEST_ASSERT_EQUAL_INT(2, e.updateCount);
    TEST_ASSERT_EQUAL_INT(1, e.resetCount);
}

void test_direct_begin_then_beginExtension_runs_begin_twice(void) {
    // Spec: a direct begin() call (e.g. user code initialising a status
    // display before Sandbox::initialize()) does NOT set _begun. The first
    // subsequent beginExtension() will still call begin(); only after
    // beginExtension() has run is _begun set, so further calls are no-ops.
    TestExtension e;
    e.begin();                                  // direct call
    Outrun::Extension::beginExtension(e);       // calls begin() again
    Outrun::Extension::beginExtension(e);       // no-op
    TEST_ASSERT_EQUAL_INT(2, e.beginCount);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_lifecycle_does_nothing);
    RUN_TEST(test_begin_if_needed_runs_once);
    RUN_TEST(test_subclass_overrides_invoked);
    RUN_TEST(test_direct_begin_then_beginExtension_runs_begin_twice);
    return UNITY_END();
}
