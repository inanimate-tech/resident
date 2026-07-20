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

// Compiles (no syntax error) but declares none of init/on_tick/on_event, so
// Sandbox::compileApp() rejects it for "no callbacks found" *before* setting
// _runState back to Running — the app stays unloaded (Ready).
constexpr const char* NO_CALLBACKS_APP = "-- no callbacks here\n";

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

void test_reload_while_held_does_not_leak_into_next_tap(void) {
  sandbox->loadApp(APP);

  // Press, but stay well under the hold threshold — "mid-hold", not yet
  // fired. This leaves _holdWasDown=true, _holdFired=false, and
  // _holdDownSince pinned at this early timestamp.
  btn->down = true;
  loopAdvance(50);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());

  // A reload lands while the button is still held (e.g. the source fails to
  // compile). loadAppInternal() unloads the running app first, so
  // isAppRunning() goes false for the remainder of this call.
  sandbox->loadApp(NO_CALLBACKS_APP);

  // The physical release happens while no app is running. updateSystemButtonHold()
  // guards on !isAppRunning() and returns immediately, so this release edge
  // is never processed — the stale press state survives untouched.
  btn->down = false;
  loopAdvance(500);                      // pushes millis() well past 500ms
                                          // *since the stale press timestamp*
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());  // still nothing observed

  // A good reload brings the app back up.
  sandbox->loadApp(APP);
  holds.clear();

  // A brief, fresh tap — nowhere near the real hold threshold.
  btn->down = true;
  loopAdvance(50);
  btn->down = false;
  loopAdvance(10);

  // Buggy behaviour (pre-fix): the stale _holdWasDown/_holdFired/_holdDownSince
  // from the interrupted mid-hold survive the reloads, so this tap's very
  // first update sees down && _holdWasDown with elapsed already far past the
  // threshold (measured against the ancient timestamp) and fires a spurious
  // enter (plus a matching exit on release) despite only ~50ms of real
  // physical contact. Fixed behaviour: the reload resets the detector, so
  // this is treated as a fresh press and nothing fires.
  for (bool held : holds) {
    TEST_ASSERT_FALSE(held);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hold_fires_enter_then_exit_while_app_running);
  RUN_TEST(test_no_hold_without_app);
  RUN_TEST(test_tap_below_threshold_does_not_fire);
  RUN_TEST(test_reload_while_held_does_not_leak_into_next_tap);
  UNITY_END();
  return 0;
}
