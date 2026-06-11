// Behavioral tests for Sandbox::suspendApp() / resumeApp() / isAppSuspended().
//
// Compiles the real ResidentSandbox.cpp against the native stub set
// (Arduino/Courier/ezTime/Esp in ../include, real ArduinoJson via lib_deps).
// The sandbox runs networkless (no cfg.network), so no Courier::Client is
// ever constructed — the stub Client only needs to satisfy the compiler.
//
// Observability: a SpyDriver extension records onAppRunning() notifications
// (what frees/suppresses the status display on real hardware) and exposes a
// `probe` Lua module the test app calls from on_tick/on_event, so tick and
// event dispatch are counted from inside the Lua VM.
#include <unity.h>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

class SpyDriver : public Resident::Driver {
public:
  int tickCount = 0;
  int eventCount = 0;
  int updateCount = 0;
  std::vector<bool> appRunningCalls;
  char lastEventName[32] = "";

  const char* name() const override { return "probe"; }
  void update() override { updateCount++; }
  void onAppRunning(bool running) override { appRunningCalls.push_back(running); }

  void registerModule(Resident::LuaModule& m) override {
    m.method<SpyDriver, &SpyDriver::luaTick>("tick");
    m.method<SpyDriver, &SpyDriver::luaEvent>("event");
  }

  int luaTick(lua_State* L) {
    (void)L;
    tickCount++;
    return 0;
  }
  int luaEvent(lua_State* L) {
    const char* n = lua_tostring(L, 1);
    if (n) {
      strncpy(lastEventName, n, sizeof(lastEventName) - 1);
      lastEventName[sizeof(lastEventName) - 1] = '\0';
    }
    eventCount++;
    return 0;
  }
};

SpyDriver* spy = nullptr;
Resident::Sandbox* sandbox = nullptr;

constexpr const char* TEST_APP =
    "function init(ctx) end\n"
    "function on_tick(ctx, dt) probe.tick() end\n"
    "function on_event(ctx, e) probe.event(e.name) end\n";

// Advance the fake clock past TICK_INTERVAL (100ms) and run one loop()
// iteration; each iteration fires at most one on_tick and dispatches at
// most one queued event.
void runLoop(int times = 1) {
  for (int i = 0; i < times; i++) {
    testMillis() += 200;
    sandbox->loop();
  }
}

}  // namespace

void setUp(void) {
  testMillis() = 0;
  spy = new SpyDriver();
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {spy};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}

void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  delete spy;
  spy = nullptr;
}

void test_suspend_resume_are_noops_without_app(void) {
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());

  sandbox->suspendApp();
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());

  sandbox->resumeApp();
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());

  TEST_ASSERT_EQUAL_INT(0, (int)spy->appRunningCalls.size());
}

void test_loaded_app_starts_running_not_suspended(void) {
  sandbox->loadApp(TEST_APP);

  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
  TEST_ASSERT_EQUAL_INT(1, (int)spy->appRunningCalls.size());
  TEST_ASSERT_TRUE(spy->appRunningCalls[0]);

  runLoop();
  TEST_ASSERT_EQUAL_INT(1, spy->tickCount);
}

void test_suspend_pauses_tick_but_extensions_keep_updating(void) {
  sandbox->loadApp(TEST_APP);
  runLoop();
  TEST_ASSERT_EQUAL_INT(1, spy->tickCount);

  sandbox->suspendApp();
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());
  // Still loaded/running — suspension is a separate axis.
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  // Drivers were told the app stopped owning the screen.
  TEST_ASSERT_FALSE(spy->appRunningCalls.back());

  int updatesBefore = spy->updateCount;
  runLoop(3);
  TEST_ASSERT_EQUAL_INT(1, spy->tickCount);  // no further ticks
  TEST_ASSERT_EQUAL_INT(updatesBefore + 3, spy->updateCount);
}

void test_resume_restores_tick_and_renotifies(void) {
  sandbox->loadApp(TEST_APP);
  sandbox->suspendApp();
  runLoop(2);
  TEST_ASSERT_EQUAL_INT(0, spy->tickCount);

  sandbox->resumeApp();
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
  TEST_ASSERT_TRUE(spy->appRunningCalls.back());

  runLoop();
  TEST_ASSERT_EQUAL_INT(1, spy->tickCount);
}

void test_repeated_suspend_resume_notify_once(void) {
  sandbox->loadApp(TEST_APP);
  TEST_ASSERT_EQUAL_INT(1, (int)spy->appRunningCalls.size());  // [true]

  sandbox->suspendApp();
  sandbox->suspendApp();  // no-op: already suspended
  TEST_ASSERT_EQUAL_INT(2, (int)spy->appRunningCalls.size());  // [true, false]

  sandbox->resumeApp();
  sandbox->resumeApp();  // no-op: not suspended
  TEST_ASSERT_EQUAL_INT(3, (int)spy->appRunningCalls.size());  // [.., true]
  TEST_ASSERT_TRUE(spy->appRunningCalls.back());
}

void test_events_deferred_while_suspended_dispatch_on_resume(void) {
  sandbox->loadApp(TEST_APP);
  sandbox->suspendApp();

  // Queued while suspended: the app is still loaded, so the event is
  // accepted — dispatch is what's gated.
  sandbox->sendAppEvent("ping", "{}");
  runLoop(2);
  TEST_ASSERT_EQUAL_INT(0, spy->eventCount);

  sandbox->resumeApp();
  runLoop();
  TEST_ASSERT_EQUAL_INT(1, spy->eventCount);
  TEST_ASSERT_EQUAL_STRING("ping", spy->lastEventName);
}

void test_long_suspend_overflows_ring_dropping_oldest(void) {
  sandbox->loadApp(TEST_APP);
  sandbox->suspendApp();

  // The 8-slot ring holds 7 events; queueing 10 drops the oldest 3.
  char name[8];
  for (int i = 1; i <= 10; i++) {
    snprintf(name, sizeof(name), "e%d", i);
    sandbox->sendAppEvent(name, "{}");
  }

  sandbox->resumeApp();
  runLoop(10);
  TEST_ASSERT_EQUAL_INT(7, spy->eventCount);
  TEST_ASSERT_EQUAL_STRING("e10", spy->lastEventName);
}

void test_load_app_clears_suspension(void) {
  sandbox->loadApp(TEST_APP);
  sandbox->suspendApp();
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->loadApp(TEST_APP);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(spy->appRunningCalls.back());

  runLoop();
  TEST_ASSERT_EQUAL_INT(1, spy->tickCount);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_suspend_resume_are_noops_without_app);
  RUN_TEST(test_loaded_app_starts_running_not_suspended);
  RUN_TEST(test_suspend_pauses_tick_but_extensions_keep_updating);
  RUN_TEST(test_resume_restores_tick_and_renotifies);
  RUN_TEST(test_repeated_suspend_resume_notify_once);
  RUN_TEST(test_events_deferred_while_suspended_dispatch_on_resume);
  RUN_TEST(test_long_suspend_overflows_ring_dropping_oldest);
  RUN_TEST(test_load_app_clears_suspension);
  UNITY_END();
  return 0;
}
