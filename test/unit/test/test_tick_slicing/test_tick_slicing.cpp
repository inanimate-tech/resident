// Behavioral tests for time-sliced on_tick execution.
//
// Contract under test: an app frame that outruns its slice budget must not
// hold the main loop. The frame yields, Sandbox::loop() returns so the rest
// of the system (Courier, driver update(), overlays, buttons) gets its turn,
// and the frame resumes on the next pass until it completes. A slow app
// degrades to a lower frame rate instead of stalling the device.
//
// Observability: a `probe` driver counts update() calls (the loop work that
// must keep running between slices) and exposes probe.burn(ms), which
// advances the fake clock from inside Lua — that is how a test app spends
// wall time mid-frame without real work. Frame progress is read back out of
// the Lua VM via Sandbox's *ForTest global accessors.
#include <unity.h>

#include "ResidentSandbox.cpp"

namespace {

class SliceProbe : public Resident::Driver {
public:
  int updateCount = 0;

  const char* name() const override { return "probe"; }
  void update() override { updateCount++; }

  void registerModule(Resident::LuaModule& m) override {
    m.method<SliceProbe, &SliceProbe::luaBurn>("burn");
  }

  // probe.burn(ms) — spend `ms` of wall time. Advancing the stub clock is the
  // native stand-in for expensive work: the slice hook reads millis(), so this
  // drives the budget exactly as real compute would on device.
  int luaBurn(lua_State* L) {
    testMillis() += (unsigned long)luaL_checkinteger(L, 1);
    return 0;
  }
};

SliceProbe* probe = nullptr;
Resident::Sandbox* sandbox = nullptr;

// ~400 ms of work per frame, spent in small increments so the count hook has
// plenty of instruction boundaries to fire on. Records ctx.time_ms at both
// ends of the frame so the freeze contract can be checked.
constexpr const char* SLOW_APP =
    "frames = 0\n"
    "done = false\n"
    "events = 0\n"
    "t_first = -1\n"
    "t_last = -1\n"
    "last_dt = -1\n"
    "function on_tick(ctx, dt)\n"
    "  frames = frames + 1\n"
    "  done = false\n"
    "  t_first = ctx.time_ms\n"
    "  last_dt = dt\n"
    "  for i = 1, 400 do probe.burn(1) end\n"
    "  t_last = ctx.time_ms\n"
    "  done = true\n"
    "end\n"
    "function on_event(ctx, e) events = events + 1 end\n";

// A frame that finishes well inside any sane slice budget.
constexpr const char* FAST_APP =
    "frames = 0\n"
    "done = false\n"
    "function on_tick(ctx, dt)\n"
    "  frames = frames + 1\n"
    "  done = true\n"
    "end\n";

void makeSandbox(uint32_t sliceMs) {
  probe = new SliceProbe();
  Resident::SandboxConfig cfg;
  cfg.deviceType  = "native-test";
  cfg.extensions  = {probe};
  cfg.persistApps = false;
  cfg.tickSliceMs = sliceMs;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}

// One loop() pass with no externally-injected time. Any clock movement comes
// from the app itself (probe.burn), so slicing is measured against the app's
// own cost rather than a synthetic jump.
void pass(int times = 1) {
  for (int i = 0; i < times; i++) sandbox->loop();
}

// Advance past TICK_INTERVAL so the next pass starts a frame.
void armTick() { testMillis() += 200; }

// Run passes until the in-flight frame reports done, bounded so a broken
// implementation fails the assert instead of hanging the suite.
int passesUntilFrameDone(int limit = 500) {
  int n = 0;
  while (n < limit && !sandbox->luaGlobalBoolForTest("done")) {
    sandbox->loop();
    n++;
  }
  return n;
}

}  // namespace

void setUp(void) {
  testMillis() = 0;
  probe = nullptr;
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete probe;   probe = nullptr;
}

// A frame inside its budget must behave exactly as before slicing existed:
// entered and completed within a single loop() pass.
void test_fast_frame_completes_in_one_pass(void) {
  makeSandbox(8);
  sandbox->loadApp(FAST_APP);

  armTick();
  pass();

  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("done"));
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("frames"));
}

// The headline contract: a frame that outruns the slice spans multiple loop()
// passes, and driver update() runs between the slices.
void test_slow_frame_yields_and_loop_work_runs_between_slices(void) {
  makeSandbox(8);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();

  // Frame started but did not finish in the first pass.
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("frames"));
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("done"));

  int updatesAtYield = probe->updateCount;
  int passes = passesUntilFrameDone();

  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("done"));
  TEST_ASSERT_GREATER_THAN_INT(0, passes);          // took more than the first pass
  TEST_ASSERT_GREATER_THAN_INT(updatesAtYield, probe->updateCount);  // loop kept working
}

// ctx is built once at frame start. A frame spanning many slices must see the
// same time_ms at its end as at its start — a moving goalpost would corrupt
// any motion computed against it.
void test_ctx_time_is_frozen_across_slices(void) {
  makeSandbox(8);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();
  passesUntilFrameDone();

  int first = sandbox->luaGlobalIntForTest("t_first");
  int last  = sandbox->luaGlobalIntForTest("t_last");
  TEST_ASSERT_GREATER_OR_EQUAL_INT(0, first);
  TEST_ASSERT_EQUAL_INT(first, last);
}

// No frame overlap: while one frame is in flight, elapsed time past
// TICK_INTERVAL must not start another.
void test_no_new_frame_starts_while_one_is_in_flight(void) {
  makeSandbox(8);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("done"));

  // The app's own burn() has already pushed the clock well past TICK_INTERVAL.
  // Keep passing; on_tick must still have been entered exactly once.
  pass(3);
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("frames"));
}

// The app is told the truth about how long its frame took: the next frame's
// dt_ms covers the whole of the previous frame, so time-based motion stays
// correct at a degraded frame rate.
void test_next_frame_dt_covers_full_previous_frame(void) {
  makeSandbox(8);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();
  passesUntilFrameDone();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("frames"));

  // Second frame: dt must account for the ~400ms the first frame spent.
  armTick();
  pass();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("frames"));
  TEST_ASSERT_GREATER_OR_EQUAL_INT(400, sandbox->luaGlobalIntForTest("last_dt"));
}

// on_event must never interleave with a suspended on_tick — an event arriving
// mid-frame waits for the frame to complete.
void test_events_do_not_dispatch_mid_frame(void) {
  makeSandbox(8);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("done"));

  sandbox->sendAppEvent("ping", "{}");
  pass(2);
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("events"));  // still mid-frame

  passesUntilFrameDone();
  pass(2);   // frame complete; the queued event may now dispatch
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("events"));
}

// Opt-out: tickSliceMs == 0 restores the un-sliced path — the whole frame
// runs to completion inside one loop() pass.
void test_slicing_disabled_runs_frame_in_one_pass(void) {
  makeSandbox(0);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();

  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("done"));
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("frames"));
}

// Loading a new app while a frame is mid-flight must abandon it cleanly and
// leave the new app running.
void test_load_app_abandons_in_flight_frame(void) {
  makeSandbox(8);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("done"));

  sandbox->loadApp(FAST_APP);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());

  armTick();
  pass();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("done"));
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("frames"));
}

// Suspending mid-frame abandons the in-flight frame; resuming starts a fresh
// one rather than continuing a stale coroutine.
void test_suspend_abandons_in_flight_frame(void) {
  makeSandbox(8);
  sandbox->loadApp(SLOW_APP);

  armTick();
  pass();
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("done"));

  sandbox->suspendApp();
  pass(3);
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("frames"));  // no progress

  sandbox->resumeApp();
  armTick();
  pass();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("frames"));  // fresh frame
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fast_frame_completes_in_one_pass);
  RUN_TEST(test_slow_frame_yields_and_loop_work_runs_between_slices);
  RUN_TEST(test_ctx_time_is_frozen_across_slices);
  RUN_TEST(test_no_new_frame_starts_while_one_is_in_flight);
  RUN_TEST(test_next_frame_dt_covers_full_previous_frame);
  RUN_TEST(test_events_do_not_dispatch_mid_frame);
  RUN_TEST(test_slicing_disabled_runs_frame_in_one_pass);
  RUN_TEST(test_load_app_abandons_in_flight_frame);
  RUN_TEST(test_suspend_abandons_in_flight_frame);
  UNITY_END();
  return 0;
}
