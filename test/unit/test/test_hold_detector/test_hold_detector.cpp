// The runtime hold detector on the SystemButton role slot: fires enter at the
// threshold and exit on release, only while an app is running.
#include <unity.h>
#include <vector>
#include "ResidentSandbox.cpp"

namespace {
class FakeButton : public Resident::SystemButton {
public:
  bool down = false;
  const char* name() const override { return "sysbtn"; }
  bool pressed() override { return down; }
};

FakeButton* btn = nullptr;
Resident::Sandbox* sandbox = nullptr;
std::vector<bool> holds;

constexpr const char* APP =
    "function init(ctx) end\nfunction on_tick(ctx, dt) end\n";

void loopAdvance(unsigned long dtMs) { testMillis() += dtMs; sandbox->loop(); }
}  // namespace

void setUp(void) {
  testMillis() = 0;
  holds.clear();
  btn = new FakeButton();
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemButton = btn;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  sandbox->onSystemButtonHold([](bool held) { holds.push_back(held); });
}
void tearDown(void) { delete sandbox; sandbox = nullptr; delete btn; btn = nullptr; }

void test_hold_fires_enter_then_exit_while_app_running(void) {
  sandbox->loadApp(APP);

  btn->down = true;
  loopAdvance(10);                       // press edge, below threshold
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());

  loopAdvance(600);                      // now past 500ms threshold
  TEST_ASSERT_EQUAL_INT(1, (int)holds.size());
  TEST_ASSERT_TRUE(holds[0]);            // enter

  btn->down = false;
  loopAdvance(10);                       // release
  TEST_ASSERT_EQUAL_INT(2, (int)holds.size());
  TEST_ASSERT_FALSE(holds[1]);           // exit
}

void test_no_hold_without_app(void) {
  // No app loaded → detector is inert (avoids clashing with boot countdown).
  btn->down = true;
  loopAdvance(10);
  loopAdvance(600);
  btn->down = false;
  loopAdvance(10);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());
}

void test_tap_below_threshold_does_not_fire(void) {
  sandbox->loadApp(APP);
  btn->down = true;
  loopAdvance(100);                      // held only 100ms
  btn->down = false;
  loopAdvance(10);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hold_fires_enter_then_exit_while_app_running);
  RUN_TEST(test_no_hold_without_app);
  RUN_TEST(test_tap_below_threshold_does_not_fire);
  UNITY_END();
  return 0;
}
