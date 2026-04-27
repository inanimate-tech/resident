#include <unity.h>
#include "OutrunDriver.h"

namespace {
class StubDriver : public Outrun::Driver {
public:
  int onAppRunningCalls = 0;
  bool lastRunning = false;
  int beginCount = 0;
  int updateCount = 0;
  int resetCount = 0;
  const char* name() const override { return "stub"; }
  void onAppRunning(bool running) override {
    onAppRunningCalls++;
    lastRunning = running;
  }
  void begin() override { beginCount++; }
  void update() override { updateCount++; }
  void onAppReset() override { resetCount++; }
};
}

void setUp(void) {}
void tearDown(void) {}

void test_driver_is_extension(void) {
    StubDriver d;
    Outrun::Extension* asExt = &d;
    TEST_ASSERT_EQUAL_STRING("stub", asExt->name());
    asExt->begin();
    asExt->update();
    asExt->onAppReset();
    TEST_ASSERT_EQUAL_INT(1, d.beginCount);
    TEST_ASSERT_EQUAL_INT(1, d.updateCount);
    TEST_ASSERT_EQUAL_INT(1, d.resetCount);
}

void test_on_app_running_dispatches(void) {
    StubDriver d;
    d.onAppRunning(true);
    d.onAppRunning(false);
    TEST_ASSERT_EQUAL_INT(2, d.onAppRunningCalls);
    TEST_ASSERT_FALSE(d.lastRunning);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_driver_is_extension);
    RUN_TEST(test_on_app_running_dispatches);
    return UNITY_END();
}
