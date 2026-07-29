// Channel routing: envelope channel "app" → on_event (data plane), channel
// "system" → reserved types + system slot (control plane), other channels →
// onMessageWithChannel registry, no channel → legacy path (deprecated).
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
Resident::Sandbox* sandbox = nullptr;

constexpr const char* APP_COUNTS_EVENTS =
    "function init(ctx) cnt = 0 end\n"
    "function on_tick(ctx, dt) end\n"
    "function on_event(ctx, e) cnt = cnt + 1; okname = (e.name == 'turn') end\n";

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}

void loadCountingApp() {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = APP_COUNTS_EVENTS;
  sandbox->injectMessage("test", "app", doc);
}

// Deliver queued events: advance past TICK_INTERVAL and tick.
void pump() { testMillis() += 200; sandbox->loop(); }
}  // namespace

void setUp(void) { testMillis() = 0; }
void tearDown(void) { delete sandbox; sandbox = nullptr; }

void test_system_channel_routes_reserved_app_load(void) {
  build();
  loadCountingApp();
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_app_channel_delivers_on_event_with_type_as_name(void) {
  build();
  loadCountingApp();
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "turn";
  evt["data"]["n"] = 1;
  sandbox->injectMessage("test", "turn", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("okname"));
}

void test_app_channel_has_no_reserved_types(void) {
  build();
  loadCountingApp();
  // type "forget" on the APP channel is just an event, not a persistence op.
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "forget";
  sandbox->injectMessage("test", "forget", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_system_channel_nonreserved_falls_to_system_slot(void) {
  build();
  static int slotCalls; static std::string slotType;
  slotCalls = 0; slotType = "";
  sandbox->onMessageWithChannel("system",
      [](const char*, const char* type, JsonDocument&) {
        slotCalls++; slotType = type;
      });
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "ota";
  sandbox->injectMessage("test", "ota", doc);
  TEST_ASSERT_EQUAL_INT(1, slotCalls);
  TEST_ASSERT_EQUAL_STRING("ota", slotType.c_str());
}

void test_system_slot_never_sees_reserved_types(void) {
  build();
  static int slotCalls; slotCalls = 0;
  sandbox->onMessageWithChannel("system",
      [](const char*, const char*, JsonDocument&) { slotCalls++; });
  loadCountingApp();
  TEST_ASSERT_EQUAL_INT(0, slotCalls);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_custom_channel_slot_and_last_wins(void) {
  build();
  static int first, second; first = second = 0;
  sandbox->onMessageWithChannel("metrics",
      [](const char*, const char*, JsonDocument&) { first++; });
  sandbox->onMessageWithChannel("metrics",
      [](const char*, const char*, JsonDocument&) { second++; });
  JsonDocument doc;
  doc["channel"] = "metrics";
  doc["type"] = "sample";
  sandbox->injectMessage("test", "sample", doc);
  TEST_ASSERT_EQUAL_INT(0, first);
  TEST_ASSERT_EQUAL_INT(1, second);
}

void test_unregistered_channel_dropped(void) {
  build();
  loadCountingApp();
  static int userCalls; userCalls = 0;
  sandbox->onMessage([](const char*, const char*, JsonDocument&) { userCalls++; });
  JsonDocument doc;
  doc["channel"] = "mystery";
  doc["type"] = "x";
  sandbox->injectMessage("test", "x", doc);
  pump();
  TEST_ASSERT_EQUAL_INT(0, userCalls);                        // not legacy onMessage
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("cnt"));  // not on_event
}

void test_unchannelled_legacy_path_still_works(void) {
  build();
  // Old-style un-channelled app load + app_event, exactly today's routing.
  JsonDocument load;
  load["type"] = "app";
  load["code"] = APP_COUNTS_EVENTS;
  sandbox->injectMessage("test", "app", load);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());

  JsonDocument evt;
  evt["type"] = "app_event";
  evt["name"] = "turn";
  evt["data"]["n"] = 1;
  sandbox->injectMessage("test", "app_event", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("okname"));
}

void test_unchannelled_filter_still_consumes(void) {
  build();
  sandbox->onMessageFilter([](const char*, const char* type, JsonDocument&) {
    return strcmp(type, "app") != 0;
  });
  JsonDocument load;
  load["type"] = "app";
  load["code"] = APP_COUNTS_EVENTS;
  sandbox->injectMessage("test", "app", load);
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
}

void test_filter_does_not_gate_channelled_messages(void) {
  build();
  sandbox->onMessageFilter([](const char*, const char*, JsonDocument&) {
    return false;  // consume everything on the legacy path
  });
  loadCountingApp();                       // channelled → filter must not run
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_app_channel_event_before_load_does_not_leak(void) {
  build();
  // No app loaded yet: an app-channel event must be dropped, not queued for
  // whatever app loads next (the event ring is not reset on app load).
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "turn";
  evt["data"]["n"] = 1;
  sandbox->injectMessage("test", "turn", evt);
  loadCountingApp();
  pump();
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("cnt"));
}

void test_defer_applies_to_system_channel_loads(void) {
  build();
  sandbox->deferAppLoads(true);
  loadCountingApp();
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->hasDeferredAppLoad());
  sandbox->deferAppLoads(false);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_app_channel_drops_self_echo(void) {
  build();
  loadCountingApp();
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "turn";
  evt["from"] = sandbox->getDeviceId();   // our own event echoed back (UDP)
  sandbox->injectMessage("test", "turn", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("cnt"));
}

void test_app_channel_dedups_by_nonce(void) {
  build();
  loadCountingApp();
  for (int i = 0; i < 2; i++) {
    JsonDocument evt;
    evt["channel"] = "app";
    evt["type"] = "turn";
    evt["from"] = "otherdev";
    evt["nonce"] = "otherdev:42";        // same nonce via local + mqtt
    sandbox->injectMessage(i == 0 ? "local" : "mqtt", "turn", evt);
  }
  // Two pumps: loop() drains one queued event per tick, so a single pump
  // would show cnt==1 even without dedup. With dedup deleted this reads 2.
  pump(); pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
}

void test_app_channel_distinct_nonces_both_deliver(void) {
  build();
  loadCountingApp();
  const char* nonces[] = {"otherdev:1", "otherdev:2"};
  for (int i = 0; i < 2; i++) {
    JsonDocument evt;
    evt["channel"] = "app";
    evt["type"] = "turn";
    evt["from"] = "otherdev";
    evt["nonce"] = nonces[i];
    sandbox->injectMessage("test", "turn", evt);
  }
  pump(); pump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("cnt"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_system_channel_routes_reserved_app_load);
  RUN_TEST(test_app_channel_delivers_on_event_with_type_as_name);
  RUN_TEST(test_app_channel_has_no_reserved_types);
  RUN_TEST(test_system_channel_nonreserved_falls_to_system_slot);
  RUN_TEST(test_system_slot_never_sees_reserved_types);
  RUN_TEST(test_custom_channel_slot_and_last_wins);
  RUN_TEST(test_unregistered_channel_dropped);
  RUN_TEST(test_unchannelled_legacy_path_still_works);
  RUN_TEST(test_unchannelled_filter_still_consumes);
  RUN_TEST(test_filter_does_not_gate_channelled_messages);
  RUN_TEST(test_app_channel_event_before_load_does_not_leak);
  RUN_TEST(test_defer_applies_to_system_channel_loads);
  RUN_TEST(test_app_channel_drops_self_echo);
  RUN_TEST(test_app_channel_dedups_by_nonce);
  RUN_TEST(test_app_channel_distinct_nonces_both_deliver);
  return UNITY_END();
}
