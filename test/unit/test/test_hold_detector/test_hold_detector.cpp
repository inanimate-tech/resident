// The runtime hold detector on the SystemButton role slot: fires enter once a
// press has lasted the threshold and exit on release, timed from the button's
// edge timestamps. It runs whenever an app is loaded or the device is idle
// (Ready) — it is inert only during the boot countdown (RunState::Pending),
// where the countdown owns the button (see test_sandbox_persistence for the
// countdown and the hand-over between the two).
#include <unity.h>
#include <vector>
#include "ResidentSandbox.cpp"
#include "FakeSystemButton.h"

namespace {
FakeSystemButton* btn = nullptr;
Resident::Sandbox* sandbox = nullptr;
std::vector<bool> holds;

constexpr const char* APP =
    "function init(ctx) end\nfunction on_tick(ctx, dt) end\n";

// Compiles (no syntax error) but declares none of init/on_tick/on_event, so
// Sandbox::compileApp() rejects it for "no callbacks found" — the app stays
// unloaded (Ready).
constexpr const char* NO_CALLBACKS_APP = "-- no callbacks here\n";

void loopAt(unsigned long ms) { testMillis() = ms; sandbox->loop(); }

void makeSandbox(bool withHoldCb) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemButton = btn;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  if (withHoldCb) sandbox->onSystemButtonHold([](bool held) { holds.push_back(held); });
}
}  // namespace

void setUp(void) {
  testMillis() = 0;
  holds.clear();
  btn = new FakeSystemButton();
  makeSandbox(/*withHoldCb=*/true);
}
void tearDown(void) { delete sandbox; sandbox = nullptr; delete btn; btn = nullptr; }

void test_hold_fires_enter_then_exit_while_app_running(void) {
  sandbox->loadApp(APP);

  btn->press(10);
  loopAt(10);                            // press edge, below threshold
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());

  loopAt(509);                           // 499 ms held: not yet
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());

  loopAt(510);                           // 500 ms held, no further edge
  TEST_ASSERT_EQUAL_INT(1, (int)holds.size());
  TEST_ASSERT_TRUE(holds[0]);            // enter

  loopAt(800);                           // still held: fires once
  TEST_ASSERT_EQUAL_INT(1, (int)holds.size());

  btn->release(810);
  loopAt(810);                           // release
  TEST_ASSERT_EQUAL_INT(2, (int)holds.size());
  TEST_ASSERT_FALSE(holds[1]);           // exit
}

void test_hold_fires_in_ready_without_app(void) {
  // No app loaded (device is Ready after setup()) → the hold gesture must
  // still fire. An embedder that never loads a Lua app (push-to-talk only)
  // stays in Ready, so the detector can only be useful there if it runs
  // outside RunState::Running too. Only RunState::Pending (the boot
  // countdown, which owns the button) gates it off.
  btn->press(10);
  loopAt(10);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());

  loopAt(610);
  TEST_ASSERT_EQUAL_INT(1, (int)holds.size());
  TEST_ASSERT_TRUE(holds[0]);            // enter

  btn->release(620);
  loopAt(620);
  TEST_ASSERT_EQUAL_INT(2, (int)holds.size());
  TEST_ASSERT_FALSE(holds[1]);           // exit
}

void test_tap_below_threshold_does_not_fire(void) {
  sandbox->loadApp(APP);
  btn->press(0);
  loopAt(0);
  btn->release(100);                     // held only 100 ms
  loopAt(100);
  loopAt(2000);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());
}

// The loop was away for the whole press (a long app callback): both edges are
// drained on one pass, and the press is timed from them, not from when the
// loop looked. A hold the loop never saw while it was down is still a hold.
void test_hold_released_before_the_loop_looked_fires_both(void) {
  sandbox->loadApp(APP);
  loopAt(0);
  btn->press(10);
  btn->release(700);
  loopAt(900);
  TEST_ASSERT_EQUAL_INT(2, (int)holds.size());
  TEST_ASSERT_TRUE(holds[0]);
  TEST_ASSERT_FALSE(holds[1]);
}

// The regression this design exists for: two quick, separate taps, each down
// when the loop looks, with a long gap between the looks. Timed from loop
// samples they read as one press held across the gap; timed from their edges
// they are two taps.
void test_two_quick_taps_across_a_long_gap_do_not_hold(void) {
  sandbox->loadApp(APP);
  loopAt(0);

  btn->press(50);
  loopAt(60);                            // the loop sees the first tap down
  // A long app callback: the first tap ends and the second begins meanwhile.
  btn->release(120);
  btn->press(600);
  loopAt(650);                           // the loop sees the second tap down
  btn->release(680);
  loopAt(700);
  loopAt(2000);

  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());
}

// The same taps with the loop away for both: four edges replayed on one pass.
void test_two_quick_taps_drained_together_do_not_hold(void) {
  sandbox->loadApp(APP);
  loopAt(0);
  btn->press(50);
  btn->release(120);
  btn->press(600);
  btn->release(680);
  loopAt(1500);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());
}

// A press is a physical gesture, not app state: it is timed from its own edges
// across an app reload, so a short press across a reload is no hold, and a hold
// already started ends at its own release.
void test_reload_while_held_does_not_change_the_press(void) {
  sandbox->loadApp(APP);

  btn->press(0);
  loopAt(0);
  sandbox->loadApp(NO_CALLBACKS_APP);    // a reload lands mid-press (and fails)
  btn->release(50);
  loopAt(500);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());   // a 50 ms press: a tap

  sandbox->loadApp(APP);
  btn->press(1000);
  loopAt(1600);
  TEST_ASSERT_EQUAL_INT(1, (int)holds.size());
  sandbox->loadApp(APP);                 // a reload mid-hold
  btn->release(1700);
  loopAt(1700);
  TEST_ASSERT_EQUAL_INT(2, (int)holds.size());
  TEST_ASSERT_TRUE(holds[0]);
  TEST_ASSERT_FALSE(holds[1]);           // the hold still ends
}

// With no hold callback the edges are still drained, so none is left queued
// to fire a gesture once a callback is registered.
void test_edges_are_drained_without_a_callback(void) {
  delete sandbox;
  makeSandbox(/*withHoldCb=*/false);
  btn->press(0);
  btn->release(700);                     // a hold nobody listened for
  loopAt(800);
  TEST_ASSERT_TRUE(btn->edges.empty());

  sandbox->onSystemButtonHold([](bool held) { holds.push_back(held); });
  loopAt(900);
  loopAt(2000);
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hold_fires_enter_then_exit_while_app_running);
  RUN_TEST(test_hold_fires_in_ready_without_app);
  RUN_TEST(test_tap_below_threshold_does_not_fire);
  RUN_TEST(test_hold_released_before_the_loop_looked_fires_both);
  RUN_TEST(test_two_quick_taps_across_a_long_gap_do_not_hold);
  RUN_TEST(test_two_quick_taps_drained_together_do_not_hold);
  RUN_TEST(test_reload_while_held_does_not_change_the_press);
  RUN_TEST(test_edges_are_drained_without_a_callback);
  UNITY_END();
  return 0;
}
