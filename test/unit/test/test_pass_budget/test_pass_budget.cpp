// One loop pass's budget: framework_tick, on_tick and one event share one
// hard deadline (steadyDeadlineMs; executionDeadlineMs for the first pass
// that ticks after a load), and a pass over its soft deadline sheds load —
// its event waits, the next tick moves later, an early framework wake is
// dropped.
#include <unity.h>
#include <chrono>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

// `clock.advance(ms)` moves the faked millis() the soft deadline and the tick
// schedule read; `clock.real_ms()` is the host's real clock, for a loop that
// must take real time.
int luaAdvanceClock(lua_State* L) {
  testMillis() += (unsigned long)luaL_checkinteger(L, 1);
  return 0;
}

int luaRealMs(lua_State* L) {
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  lua_pushinteger(L, (lua_Integer)(ms & 0x3fffffff));
  return 1;
}

class ClockExtension : public Resident::Extension {
public:
  const char* name() const override { return "clock"; }
  void registerModule(Resident::LuaModule& m) override {
    m.staticMethod("advance", luaAdvanceClock);
    m.staticMethod("real_ms", luaRealMs);
  }
};

// Spins while _G.fw_spin is set; returns _G.wake as its next wake.
constexpr const char* FRAMEWORK =
    "function framework_tick(ctx, dt)\n"
    "  _G.fw_ticks = (_G.fw_ticks or 0) + 1\n"
    "  if _G.fw_spin == _G.fw_ticks then for i = 1, 100000000 do end end\n"
    "  return _G.wake\n"
    "end\n";

ClockExtension clockExtension;
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;

struct Budget {
  uint32_t hardMs;
  uint32_t steadyMs;
  uint32_t softMs;
};

void build(Budget b, const char* app, const char* fw = nullptr) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.executionDeadlineMs = b.hardMs;
  cfg.steadyDeadlineMs = b.steadyMs;
  cfg.executionSoftDeadlineMs = b.softMs;
  cfg.extensions = {&clockExtension};
  if (fw) {
    Resident::SandboxConfig::FrameworkConfig f;
    f.name = "testfw";
    f.version = 1;
    f.source = fw;
    cfg.framework = f;
  }
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setTelemetryCallback([](const char* json) {
    telemetry->push_back(json ? json : "");
  });
  sandbox->setup();
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = app;
  sandbox->injectMessage("test", "app", doc);
}

void injectAppEvent(const char* name) {
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = name;
  evt["data"].to<JsonObject>();
  sandbox->injectMessage("test", name, evt);
}

// One pass, `stepMs` after the last; returns its real milliseconds.
long timedPass(unsigned long stepMs) {
  testMillis() += stepMs;
  const auto t0 = std::chrono::steady_clock::now();
  sandbox->loop();
  return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0)
      .count();
}

// Passes every millisecond until `untilMs` (absolute faked time).
void runUntil(unsigned long untilMs) {
  while (testMillis() < untilMs) {
    testMillis() += 1;
    sandbox->loop();
  }
}

bool telemetryMentions(const char* needle) {
  for (auto& t : *telemetry)
    if (t.find(needle) != std::string::npos) return true;
  return false;
}

int global(const char* name) { return sandbox->luaGlobalIntForTest(name); }

}  // namespace

void setUp(void) {
  testMillis() = 1000;
  telemetry = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  delete telemetry;
  telemetry = nullptr;
}

// framework_tick and on_tick both run away in the same pass. Each has its
// own executionDeadlineMs (400 ms) if dispatches are budgeted one by one;
// the pass as a whole has 60 ms.
void test_a_pass_spends_one_deadline_not_one_per_dispatch(void) {
  build({400, 60, 0},
        "ticks = 0\n"
        "fw_spin = 2\n"
        "function on_tick(ctx, dt)\n"
        "  ticks = ticks + 1\n"
        "  if ticks == 2 then for i = 1, 100000000 do end end\n"
        "end\n"
        "function on_event(ctx, e) seen = ticks end\n",
        FRAMEWORK);
  timedPass(100);                    // first pass: settles the load
  TEST_ASSERT_EQUAL_INT(1, global("ticks"));
  injectAppEvent("ping");
  const long ms = timedPass(100);
  TEST_ASSERT_LESS_THAN_INT32(350, ms);
  TEST_ASSERT_TRUE(telemetryMentions("execution deadline exceeded (60 ms)"));
  // The pass ran out of time: on_tick never started, and the event waits.
  TEST_ASSERT_EQUAL_INT(1, global("ticks"));
  TEST_ASSERT_EQUAL_INT(0, global("seen"));
  timedPass(0);                      // no tick due: the event alone
  TEST_ASSERT_EQUAL_INT(1, global("seen"));
}

// The first pass that ticks after a load has executionDeadlineMs, for the
// work an app does once; later passes have steadyDeadlineMs.
void test_first_pass_after_a_load_gets_the_full_deadline(void) {
  build({400, 60, 0},
        "ticks = 0\n"
        "function on_tick(ctx, dt)\n"
        "  ticks = ticks + 1\n"
        "  local t0 = clock.real_ms()\n"
        "  while clock.real_ms() - t0 < 150 do end\n"
        "  done = ticks\n"
        "end\n");
  timedPass(100);
  TEST_ASSERT_EQUAL_INT(1, global("done"));
  TEST_ASSERT_FALSE(telemetryMentions("execution deadline exceeded"));
  timedPass(100);
  TEST_ASSERT_EQUAL_INT(1, global("done"));
  TEST_ASSERT_TRUE(telemetryMentions("execution deadline exceeded (60 ms)"));
}

// steadyDeadlineMs only tightens executionDeadlineMs.
void test_steady_deadline_never_loosens_the_hard_one(void) {
  build({60, 400, 0},
        "ticks = 0\n"
        "function on_tick(ctx, dt)\n"
        "  ticks = ticks + 1\n"
        "  if ticks == 2 then for i = 1, 100000000 do end end\n"
        "end\n");
  timedPass(100);
  timedPass(100);
  TEST_ASSERT_TRUE(telemetryMentions("execution deadline exceeded (60 ms)"));
}

// A pass over its soft deadline leaves its event for the next pass.
void test_slow_pass_leaves_its_event_for_the_next_pass(void) {
  build({1000, 250, 20},
        "ticks = 0\n"
        "function on_tick(ctx, dt) ticks = ticks + 1 clock.advance(30) end\n"
        "function on_event(ctx, e) seen = ticks end\n");
  injectAppEvent("ping");
  timedPass(100);
  TEST_ASSERT_EQUAL_INT(1, global("ticks"));
  TEST_ASSERT_EQUAL_INT(0, global("seen"));
  TEST_ASSERT_TRUE(telemetryMentions("slow_dispatch"));
  timedPass(0);
  TEST_ASSERT_EQUAL_INT(1, global("seen"));
}

// The next tick moves later by the overrun: 50 ms against a 20 ms soft
// deadline puts it 30 ms after the 100 ms interval.
void test_slow_pass_moves_the_next_tick_by_the_overrun(void) {
  build({1000, 250, 20},
        "ticks = 0\n"
        "function on_tick(ctx, dt)\n"
        "  ticks = ticks + 1\n"
        "  if ticks == 1 then clock.advance(50) end\n"
        "end\n");
  runUntil(1100);                    // tick 1, which runs to 1150
  TEST_ASSERT_EQUAL_INT(1, global("ticks"));
  runUntil(1229);
  TEST_ASSERT_EQUAL_INT(1, global("ticks"));
  runUntil(1230);
  TEST_ASSERT_EQUAL_INT(2, global("ticks"));
}

// A framework wake due before that later tick is dropped.
void test_slow_pass_drops_framework_wakes_before_the_next_tick(void) {
  build({1000, 250, 20},
        "wake = 5\n"
        "ticks = 0\n"
        "function on_tick(ctx, dt)\n"
        "  ticks = ticks + 1\n"
        "  if ticks == 1 then clock.advance(50) end\n"
        "end\n",
        FRAMEWORK);
  runUntil(1100);
  TEST_ASSERT_EQUAL_INT(1, global("fw_ticks"));
  runUntil(1229);
  TEST_ASSERT_EQUAL_INT(1, global("fw_ticks"));
  runUntil(1230);
  TEST_ASSERT_EQUAL_INT(2, global("fw_ticks"));
  runUntil(1240);                    // in time again: wakes resume
  TEST_ASSERT_GREATER_THAN_INT(2, global("fw_ticks"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_pass_spends_one_deadline_not_one_per_dispatch);
  RUN_TEST(test_first_pass_after_a_load_gets_the_full_deadline);
  RUN_TEST(test_steady_deadline_never_loosens_the_hard_one);
  RUN_TEST(test_slow_pass_leaves_its_event_for_the_next_pass);
  RUN_TEST(test_slow_pass_moves_the_next_tick_by_the_overrun);
  RUN_TEST(test_slow_pass_drops_framework_wakes_before_the_next_tick);
  return UNITY_END();
}
