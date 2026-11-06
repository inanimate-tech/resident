// The LVGL pump under the execution guard: LvglModule::update runs
// lv_timer_handler as one guarded dispatch, so luavgl's callbacks (here, its
// timers) are stopped at the sandbox's deadline like a tick is.
//
// Real LVGL and luavgl on the host (env:native-lvgl), drawing to a fake
// panel. As in test_execution_deadline, every runaway is a finite loop of a
// second or more, and each test is a limit check: the guard must end the pump
// within a fraction of that, so a regression fails on time instead of
// hanging.
#include <unity.h>
#include <chrono>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"
#include "ResidentLvglModule.h"

namespace {

constexpr uint32_t kDeadlineMs = 50;
// The deadline plus generous slack for a loaded CI host.
constexpr long kLimitMs = 750;

#define SPIN "for i = 1, 100000000 do end\n"

// Counts what LVGL flushes to it.
class FakePanel : public Resident::PanelTarget {
public:
  int32_t width() const override { return 32; }
  int32_t height() const override { return 16; }
  void blit(int32_t, int32_t, int32_t, int32_t, const uint16_t*) override { blits++; }
  void frameDone() override { frames++; }
  int blits = 0;
  int frames = 0;
};

FakePanel panel;
Resident::LvglModule lvglModule;
// One sandbox for the suite, as on a device: one Lua state and one LVGL for
// its life, with each test loading its own app over the last one.
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string> telemetry;

void build() {
  Resident::RenderTargets::addPanel("main", &panel);
  lvglModule.addDisplay("main");
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.executionDeadlineMs = kDeadlineMs;
  cfg.extensions = {&lvglModule};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setTelemetryCallback([](const char* json) {
    telemetry.push_back(json ? json : "");
  });
  sandbox->setup();
}

void loadApp(const char* code) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  sandbox->injectMessage("test", "app", doc);
}

int telemetryCount(const char* needle) {
  int n = 0;
  for (auto& t : telemetry)
    if (t.find(needle) != std::string::npos) n++;
  return n;
}

// One loop pass that is due a tick and a pump (LVGL's tick is millis(), and
// every timer below is due after 100 ms); returns its wall-clock ms.
long timedPump() {
  testMillis() += 200;
  const auto t0 = std::chrono::steady_clock::now();
  sandbox->loop();
  return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0)
      .count();
}

int globalInt(const char* name) { return sandbox->luaGlobalIntForTest(name); }

}  // namespace

void setUp(void) { telemetry.clear(); }

void tearDown(void) {}

// A timer callback that catches the deadline's error with pcall and loops
// on: luavgl's own protected call, and the app's inside it.
void test_runaway_in_a_pcall_wrapped_callback_is_stopped(void) {
  loadApp(
      "local h = lvgl.bind('main')\n"
      "local function spin() " SPIN "end\n"
      "lvgl.Timer { period = 100, cb = function()\n"
      "  for n = 1, 3 do pcall(spin) end\n"
      "  finished = true\n"
      "end }\n"
      "function on_tick() end\n");
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("compile_error"));
  const long ms = timedPump();
  TEST_ASSERT_LESS_THAN_INT32(kLimitMs, ms);
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("finished"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("\"name\":\"runtime_error\""));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("lvgl: execution deadline exceeded"));
}

// Two runaway timers in one pump share one deadline: the second stops at its
// first instruction, so the pump costs one deadline, not two, and is
// reported once.
void test_two_runaways_in_one_pump_are_both_stopped(void) {
  loadApp(
      "local h = lvgl.bind('main')\n"
      "a, b = 0, 0\n"
      "lvgl.Timer { period = 100, cb = function() a = 1; " SPIN " a = 2 end }\n"
      "lvgl.Timer { period = 100, cb = function() b = 1; " SPIN " b = 2 end }\n"
      "function on_tick() end\n");
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("compile_error"));
  const long ms = timedPump();
  TEST_ASSERT_LESS_THAN_INT32(kLimitMs, ms);
  // Each ran into the deadline (one may not have got its first instruction
  // in), and neither finished.
  TEST_ASSERT_NOT_EQUAL_INT(2, globalInt("a"));
  TEST_ASSERT_NOT_EQUAL_INT(2, globalInt("b"));
  TEST_ASSERT_TRUE(globalInt("a") + globalInt("b") >= 1);
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("lvgl: execution deadline exceeded"));
}

// After a pump the deadline ended, LVGL carries on as it was: the next pump
// runs every timer to completion, redraws what changed, and is not slowed.
void test_next_pump_after_a_trip_runs_normally(void) {
  loadApp(
      "local h = lvgl.bind('main')\n"
      "label = h.Label { text = 'start' }\n"
      "runs, done, tripped = 0, 0, false\n"
      "lvgl.Timer { period = 100, cb = function()\n"
      "  if not tripped then tripped = true; " SPIN " end\n"
      "end }\n"
      "lvgl.Timer { period = 100, cb = function()\n"
      "  runs = runs + 1\n"
      "  label:set { text = 'run ' .. runs }\n"
      "  done = runs\n"
      "end }\n"
      "function on_tick() end\n");
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("compile_error"));
  TEST_ASSERT_LESS_THAN_INT32(kLimitMs, timedPump());
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("lvgl: execution deadline exceeded"));
  const int runsAfterTrip = globalInt("runs");

  telemetry.clear();
  const int blits = panel.blits;
  const long ms = timedPump();
  TEST_ASSERT_LESS_THAN_INT32((long)kDeadlineMs, ms);
  TEST_ASSERT_EQUAL_INT(runsAfterTrip + 1, globalInt("runs"));
  TEST_ASSERT_EQUAL_INT(globalInt("runs"), globalInt("done"));
  TEST_ASSERT_GREATER_THAN_INT(blits, panel.blits);   // the label redrew
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("runtime_error"));
}

// A pump with no Lua in it costs nothing and reports nothing.
void test_well_behaved_pump_reports_nothing(void) {
  loadApp(
      "local h = lvgl.bind('main')\n"
      "n = 0\n"
      "lvgl.Timer { period = 100, cb = function() n = n + 1 end }\n"
      "function on_tick() end\n");
  for (int i = 0; i < 5; i++) TEST_ASSERT_LESS_THAN_INT32((long)kDeadlineMs, timedPump());
  TEST_ASSERT_EQUAL_INT(5, globalInt("n"));
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("runtime_error"));
}

int main(int, char**) {
  build();
  UNITY_BEGIN();
  RUN_TEST(test_runaway_in_a_pcall_wrapped_callback_is_stopped);
  RUN_TEST(test_two_runaways_in_one_pump_are_both_stopped);
  RUN_TEST(test_next_pump_after_a_trip_runs_normally);
  RUN_TEST(test_well_behaved_pump_reports_nothing);
  return UNITY_END();
}
