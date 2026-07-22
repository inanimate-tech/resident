// Message interposition: onMessageFilter runs before deferral, reserved-type
// routing, and the user onMessage callback; deferAppLoads stashes app/shader
// loads (last one wins) and applies the stash when cleared. injectMessage is
// the transport-independent entry into that pipeline.
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
Resident::Sandbox* sandbox = nullptr;

constexpr const char* APP_A =
    "function init(ctx) flagA = true end\nfunction on_tick(ctx, dt) end\n";
constexpr const char* APP_B =
    "function init(ctx) flagB = true end\nfunction on_tick(ctx, dt) end\n";

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}

void injectApp(const char* code) {
  JsonDocument doc;
  doc["type"] = "app";
  doc["code"] = code;
  sandbox->injectMessage("test", "app", doc);
}
}  // namespace

void setUp(void) { testMillis() = 0; }
void tearDown(void) { delete sandbox; sandbox = nullptr; }

void test_inject_routes_reserved_types(void) {
  build();
  injectApp(APP_A);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("flagA"));
}

void test_filter_consumes_message(void) {
  build();
  sandbox->onMessageFilter([](const char*, const char* type, JsonDocument&) {
    return strcmp(type, "app") != 0;   // swallow app loads
  });
  injectApp(APP_A);
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
}

void test_filter_passes_message_through(void) {
  build();
  int seen = 0;
  sandbox->onMessageFilter([&](const char*, const char*, JsonDocument&) {
    seen++;
    return true;
  });
  injectApp(APP_A);
  TEST_ASSERT_EQUAL_INT(1, seen);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_filter_runs_before_user_callback(void) {
  build();
  static int order = 0;
  static int filterAt = 0, userAt = 0;
  order = filterAt = userAt = 0;
  sandbox->onMessageFilter([](const char*, const char*, JsonDocument&) {
    filterAt = ++order;
    return true;
  });
  sandbox->onMessage([](const char*, const char*, JsonDocument&) {
    userAt = ++order;
  });
  JsonDocument doc;
  doc["type"] = "custom";
  sandbox->injectMessage("test", "custom", doc);
  TEST_ASSERT_EQUAL_INT(1, filterAt);
  TEST_ASSERT_EQUAL_INT(2, userAt);
}

void test_defer_stashes_then_applies(void) {
  build();
  sandbox->deferAppLoads(true);
  injectApp(APP_A);
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->hasDeferredAppLoad());

  sandbox->deferAppLoads(false);
  TEST_ASSERT_FALSE(sandbox->hasDeferredAppLoad());
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("flagA"));
}

void test_defer_last_one_wins(void) {
  build();
  sandbox->deferAppLoads(true);
  injectApp(APP_A);
  injectApp(APP_B);
  sandbox->deferAppLoads(false);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("flagA"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("flagB"));
}

void test_defer_does_not_touch_other_types(void) {
  build();
  static int userCalls = 0;
  userCalls = 0;
  sandbox->deferAppLoads(true);
  sandbox->onMessage([](const char*, const char*, JsonDocument&) { userCalls++; });
  JsonDocument doc;
  doc["type"] = "custom";
  sandbox->injectMessage("test", "custom", doc);
  TEST_ASSERT_EQUAL_INT(1, userCalls);          // flowed normally
  TEST_ASSERT_FALSE(sandbox->hasDeferredAppLoad());
}

void test_filter_runs_before_deferral(void) {
  build();
  sandbox->deferAppLoads(true);
  sandbox->onMessageFilter([](const char*, const char* type, JsonDocument&) {
    return strcmp(type, "app") != 0;   // consume app before deferral sees it
  });
  injectApp(APP_A);
  TEST_ASSERT_FALSE(sandbox->hasDeferredAppLoad());
}

void test_clearing_empty_defer_is_noop(void) {
  build();
  sandbox->deferAppLoads(true);
  sandbox->deferAppLoads(false);
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_FALSE(sandbox->hasDeferredAppLoad());
}

void test_deferred_apply_does_not_refilter(void) {
  // The filter accepted the message once, at receipt. Applying the stash must
  // route it straight through — re-running the filter would let a dedup /
  // self-echo filter drop the message on its second sight, and the deferred
  // app would silently never load.
  build();
  static int appFilterCalls = 0;
  appFilterCalls = 0;
  sandbox->onMessageFilter([](const char*, const char* type, JsonDocument&) {
    if (strcmp(type, "app") != 0) return true;
    return ++appFilterCalls == 1;          // dedup: only the first app passes
  });
  sandbox->deferAppLoads(true);
  injectApp(APP_A);                        // filtered once (passes), then stashed
  TEST_ASSERT_TRUE(sandbox->hasDeferredAppLoad());

  sandbox->deferAppLoads(false);           // apply must not re-run the filter
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("flagA"));
  TEST_ASSERT_EQUAL_INT(1, appFilterCalls);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_inject_routes_reserved_types);
  RUN_TEST(test_filter_consumes_message);
  RUN_TEST(test_filter_passes_message_through);
  RUN_TEST(test_filter_runs_before_user_callback);
  RUN_TEST(test_defer_stashes_then_applies);
  RUN_TEST(test_defer_last_one_wins);
  RUN_TEST(test_defer_does_not_touch_other_types);
  RUN_TEST(test_filter_runs_before_deferral);
  RUN_TEST(test_clearing_empty_defer_is_noop);
  RUN_TEST(test_deferred_apply_does_not_refilter);
  UNITY_END();
  return 0;
}
