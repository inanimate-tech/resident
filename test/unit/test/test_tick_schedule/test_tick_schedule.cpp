// The tick schedule: the board's interval, a rate that holds when passes come
// late, a missed tick skipped rather than made up, and a framework that asks
// to run before the next tick.
#include <unity.h>

#include "ResidentSandbox.cpp"

namespace {

// Counts its ticks and the dt of the last one, and returns `wake` (a global
// the test sets) as the ms until it next wants to run.
constexpr const char* FRAMEWORK =
    "function framework_tick(ctx, dt)\n"
    "  _G.fw_ticks = (_G.fw_ticks or 0) + 1\n"
    "  _G.fw_dt = dt\n"
    "  return _G.wake\n"
    "end\n";

constexpr const char* APP =
    "ticks = 0\n"
    "function on_tick(ctx, dt) ticks = ticks + 1; last_dt = dt end\n";

// Counts its tick calls by the third argument: frames (a tick) and wakes (an
// early run the framework asked for).
constexpr const char* FRAMEWORK_FRAMES =
    "function framework_tick(ctx, dt, is_frame)\n"
    "  if is_frame then _G.fw_frames = (_G.fw_frames or 0) + 1\n"
    "  elseif is_frame == false then _G.fw_wakes = (_G.fw_wakes or 0) + 1 end\n"
    "  return _G.wake\n"
    "end\n";

Resident::Sandbox* sandbox = nullptr;

// Counts afterTick calls, and records the app's tick count at the last one,
// to show it runs after on_tick.
class AfterTickProbe : public Resident::Extension {
 public:
  const char* name() const override { return "probe"; }
  void afterTick() override {
    calls++;
    ticksSeen = sandbox->luaGlobalIntForTest("ticks");
  }
  int calls = 0;
  int ticksSeen = -1;
};
AfterTickProbe probe;

void build(uint16_t intervalMs, const char* fwSource = nullptr, const char* app = APP) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.extensions = {&probe};
  if (intervalMs) cfg.tickIntervalMs = intervalMs;
  if (fwSource) {
    Resident::SandboxConfig::FrameworkConfig fw;
    fw.name = "testfw";
    fw.version = 1;
    fw.source = fwSource;
    cfg.framework = fw;
  }
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = app;
  sandbox->injectMessage("test", "app", doc);
}

// Runs loop() once every `stepMs` until `forMs` has passed.
void run(unsigned long forMs, unsigned long stepMs) {
  for (unsigned long t = 0; t < forMs; t += stepMs) {
    testMillis() += stepMs;
    sandbox->loop();
  }
}

int ticks() { return sandbox->luaGlobalIntForTest("ticks"); }
int fwTicks() { return sandbox->luaGlobalIntForTest("fw_ticks"); }

}  // namespace

void setUp(void) {
  testMillis() = 1000;
  probe = AfterTickProbe();
}
void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
}

void test_default_interval_is_100_ms(void) {
  build(0);
  run(99, 1);
  TEST_ASSERT_EQUAL_INT(0, ticks());
  run(1, 1);
  TEST_ASSERT_EQUAL_INT(1, ticks());
  TEST_ASSERT_EQUAL_INT(100, sandbox->luaGlobalIntForTest("last_dt"));
}

void test_the_board_sets_the_interval(void) {
  build(33);
  run(990, 1);
  TEST_ASSERT_EQUAL_INT(30, ticks());
  TEST_ASSERT_EQUAL_INT(33, sandbox->luaGlobalIntForTest("last_dt"));
}

void test_the_rate_holds_when_passes_come_late(void) {
  // A pass every 30 ms against a 100 ms interval: each tick lands up to 20 ms
  // late, and the next is still due 100 ms after the last was due.
  build(100);
  run(3000, 30);
  TEST_ASSERT_INT_WITHIN(1, 30, ticks());
}

void test_a_missed_tick_is_skipped_not_made_up(void) {
  build(100);
  run(100, 1);
  TEST_ASSERT_EQUAL_INT(1, ticks());
  testMillis() += 550;   // one pass stalled for five intervals
  sandbox->loop();
  TEST_ASSERT_EQUAL_INT(2, ticks());
  TEST_ASSERT_EQUAL_INT(550, sandbox->luaGlobalIntForTest("last_dt"));
  run(99, 1);
  TEST_ASSERT_EQUAL_INT(2, ticks());
  run(1, 1);
  TEST_ASSERT_EQUAL_INT(3, ticks());
}

void test_a_framework_returning_nothing_ticks_with_the_app(void) {
  build(100, FRAMEWORK);
  run(1000, 1);
  TEST_ASSERT_EQUAL_INT(10, ticks());
  TEST_ASSERT_EQUAL_INT(10, fwTicks());
}

void test_a_framework_runs_again_when_it_asks(void) {
  build(100, FRAMEWORK,
        "wake = 20\n"
        "ticks = 0\n"
        "function on_tick(ctx, dt) ticks = ticks + 1 end\n");
  run(1000, 1);
  TEST_ASSERT_EQUAL_INT(10, ticks());
  // From the first tick at 100 ms, every 20 ms.
  TEST_ASSERT_INT_WITHIN(1, 46, fwTicks());
  TEST_ASSERT_EQUAL_INT(20, sandbox->luaGlobalIntForTest("fw_dt"));
}

void test_a_framework_wake_has_a_floor(void) {
  build(100, FRAMEWORK,
        "wake = 0\n"
        "function on_tick(ctx, dt) end\n");
  run(500, 1);
  // From the first tick at 100 ms, 4 ms apart at the closest.
  TEST_ASSERT_INT_WITHIN(1, 101, fwTicks());
}

void test_the_framework_is_told_which_calls_are_frames(void) {
  build(100, FRAMEWORK_FRAMES,
        "wake = 20\n"
        "ticks = 0\n"
        "function on_tick(ctx, dt) ticks = ticks + 1 end\n");
  run(1000, 1);
  TEST_ASSERT_EQUAL_INT(10, ticks());
  TEST_ASSERT_EQUAL_INT(10, sandbox->luaGlobalIntForTest("fw_frames"));
  // Every 20 ms from the first tick at 100 ms, less the ten frames.
  TEST_ASSERT_INT_WITHIN(1, 36, sandbox->luaGlobalIntForTest("fw_wakes"));
}

void test_extensions_hear_after_each_tick_not_each_wake(void) {
  build(100, FRAMEWORK,
        "wake = 20\n"
        "ticks = 0\n"
        "function on_tick(ctx, dt) ticks = ticks + 1 end\n");
  run(1000, 1);
  TEST_ASSERT_EQUAL_INT(10, ticks());
  TEST_ASSERT_EQUAL_INT(10, probe.calls);
  // The last call came after that tick's on_tick.
  TEST_ASSERT_EQUAL_INT(10, probe.ticksSeen);
}

void test_no_after_tick_while_no_app_runs(void) {
  build(100, nullptr, "error('no app')\n");
  run(500, 1);
  TEST_ASSERT_EQUAL_INT(0, probe.calls);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_interval_is_100_ms);
  RUN_TEST(test_the_board_sets_the_interval);
  RUN_TEST(test_the_rate_holds_when_passes_come_late);
  RUN_TEST(test_a_missed_tick_is_skipped_not_made_up);
  RUN_TEST(test_a_framework_returning_nothing_ticks_with_the_app);
  RUN_TEST(test_a_framework_runs_again_when_it_asks);
  RUN_TEST(test_a_framework_wake_has_a_floor);
  RUN_TEST(test_the_framework_is_told_which_calls_are_frames);
  RUN_TEST(test_extensions_hear_after_each_tick_not_each_wake);
  RUN_TEST(test_no_after_tick_while_no_app_runs);
  return UNITY_END();
}
