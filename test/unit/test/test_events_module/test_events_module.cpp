// events.send: data-plane emitter. Envelope shape, rate limiting, sink
// override, and the publishEvent C++ path used by wrapper aliases.
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
Resident::Sandbox* sandbox = nullptr;
std::string captured;
int sinkCalls = 0;

constexpr const char* APP_SENDS_ON_INIT =
    "function init(ctx) ok = events.send('ping', {n = 1, tag = 'x'}) end\n"
    "function on_tick(ctx, dt) end\n";

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  captured.clear();
  sinkCalls = 0;
  sandbox->setEventSink([](JsonDocument& doc) {
    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    captured = buf;
    sinkCalls++;
    return true;
  });
}

void loadApp(const char* code) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  sandbox->injectMessage("test", "app", doc);
}
}  // namespace

void setUp(void) { testMillis() = 0; }
void tearDown(void) { delete sandbox; sandbox = nullptr; }

void test_events_send_builds_app_channel_envelope(void) {
  build();
  loadApp(APP_SENDS_ON_INIT);
  TEST_ASSERT_EQUAL_INT(1, sinkCalls);
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok"));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"channel\":\"app\""));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"type\":\"ping\""));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"n\":1"));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"tag\":\"x\""));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"nonce\":"));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), sandbox->getDeviceId().c_str()));
}

void test_publish_event_cpp_path(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->publishEvent("score", "{\"v\":9}"));
  TEST_ASSERT_EQUAL_INT(1, sinkCalls);
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"type\":\"score\""));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"v\":9"));
}

void test_publish_event_rate_limited(void) {
  build();
  int sent = 0;
  for (int i = 0; i < 20; i++) {
    if (sandbox->publishEvent("spam", "{}")) sent++;
  }
  TEST_ASSERT_EQUAL_INT(10, sent);     // burst of 10, no time passing
  testMillis() += 1000;                // 1s → 5 tokens refill
  sent = 0;
  for (int i = 0; i < 20; i++) {
    if (sandbox->publishEvent("spam", "{}")) sent++;
  }
  TEST_ASSERT_EQUAL_INT(5, sent);
}

void test_nonces_are_unique_and_deviceid_prefixed(void) {
  build();
  sandbox->publishEvent("a", "{}");
  std::string first = captured;
  sandbox->publishEvent("b", "{}");
  TEST_ASSERT_TRUE(first != captured);
  char prefix[80];
  snprintf(prefix, sizeof(prefix), "\"nonce\":\"%s:", sandbox->getDeviceId().c_str());
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), prefix));
}

void test_publish_event_without_sink_or_network_returns_false(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  TEST_ASSERT_FALSE(sandbox->publishEvent("x", "{}"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_events_send_builds_app_channel_envelope);
  RUN_TEST(test_publish_event_cpp_path);
  RUN_TEST(test_publish_event_rate_limited);
  RUN_TEST(test_nonces_are_unique_and_deviceid_prefixed);
  RUN_TEST(test_publish_event_without_sink_or_network_returns_false);
  return UNITY_END();
}
