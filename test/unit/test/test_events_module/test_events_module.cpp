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

// Serializer harness: evaluates `return <tableExpr>` on a bare lua_State and
// runs serializeLuaTableToJson (same TU — this file includes
// ResidentSandbox.cpp) over the result. cap defaults small enough to test
// overflow but large enough for every non-overflow case here.
std::string serialize(const char* tableExpr, size_t cap = 512) {
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  std::string src = std::string("return ") + tableExpr;
  if (luaL_dostring(L, src.c_str()) != LUA_OK) {
    std::string err = lua_tostring(L, -1);
    lua_close(L);
    return "<lua error: " + err + ">";
  }
  char buf[512];
  if (cap > sizeof(buf)) cap = sizeof(buf);
  bool ok = Resident::serializeLuaTableToJson(L, -1, buf, cap);
  lua_close(L);
  return ok ? std::string(buf) : std::string("<overflow>");
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

// ── serializeLuaTableToJson: byte-exact unit tests ─────────────────────────
// Single-key tables where byte-exactness matters (lua_next order is
// unspecified across multiple keys); strstr assertions for multi-key tables.

void test_serializer_flat_payloads_unchanged(void) {
  // The pre-upgrade wire bytes for flat string/number tables — must not move.
  TEST_ASSERT_EQUAL_STRING("{}", serialize("{}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"n\":1}", serialize("{n = 1}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"f\":1.5}", serialize("{f = 1.5}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"big\":1e+20}", serialize("{big = 1e20}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"neg\":-7}", serialize("{neg = -7}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"tag\":\"x\"}", serialize("{tag = 'x'}").c_str());
}

void test_serializer_escapes_quotes_backslashes_controls(void) {
  TEST_ASSERT_EQUAL_STRING("{\"m\":\"say \\\"hi\\\"\"}",
                           serialize("{m = 'say \"hi\"'}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"p\":\"a\\\\b\"}",
                           serialize("{p = 'a\\\\b'}").c_str());  // Lua a\b
  TEST_ASSERT_EQUAL_STRING("{\"nl\":\"l1\\nl2\\t.\\r\"}",
                           serialize("{nl = 'l1\\nl2\\t.\\r'}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"c\":\"a\\u0001b\"}",
                           serialize("{c = 'a\\1b'}").c_str());
}

void test_serializer_escapes_keys(void) {
  TEST_ASSERT_EQUAL_STRING("{\"a\\\"b\":1}",
                           serialize("{['a\"b'] = 1}").c_str());
}

void test_serializer_booleans(void) {
  TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", serialize("{ok = true}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"off\":false}", serialize("{off = false}").c_str());
}

void test_serializer_nested_objects_to_depth_3(void) {
  TEST_ASSERT_EQUAL_STRING("{\"a\":{\"b\":{\"c\":1}}}",
                           serialize("{a = {b = {c = 1}}}").c_str());
  // A table that would sit at depth 4 is skipped along with its key,
  // like any other unsupported value.
  TEST_ASSERT_EQUAL_STRING("{\"a\":{\"b\":{}}}",
                           serialize("{a = {b = {c = {d = 1}}}}").c_str());
}

void test_serializer_nested_arrays(void) {
  TEST_ASSERT_EQUAL_STRING("{\"xs\":[1,2,3]}",
                           serialize("{xs = {1, 2, 3}}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"xs\":[\"a\",true,1.5]}",
                           serialize("{xs = {'a', true, 1.5}}").c_str());
  // Array elements may themselves be objects (still bounded by depth 3).
  TEST_ASSERT_EQUAL_STRING("{\"xs\":[{\"n\":1}]}",
                           serialize("{xs = {{n = 1}}}").c_str());
  // Unsupported array elements hold their position as null.
  TEST_ASSERT_EQUAL_STRING("{\"xs\":[1,null,3]}",
                           serialize("{xs = {1, print, 3}}").c_str());
}

void test_serializer_top_level_is_always_an_object(void) {
  // Integer keys are skipped at the top level, exactly as before the upgrade.
  TEST_ASSERT_EQUAL_STRING("{}", serialize("{1, 2, 3}").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"x\":1}", serialize("{5, x = 1}").c_str());
}

void test_serializer_skips_unsupported_values(void) {
  TEST_ASSERT_EQUAL_STRING("{\"n\":1}", serialize("{n = 1, f = print}").c_str());
  TEST_ASSERT_EQUAL_STRING("{}", serialize("{f = print}").c_str());
}

void test_serializer_overflow_returns_false(void) {
  TEST_ASSERT_EQUAL_STRING("<overflow>",
                           serialize("{k = string.rep('x', 600)}", 512).c_str());
  // Boundary: {"k":"xx…"} is 8 + n chars and cap 16 leaves room for 15 chars
  // + NUL, so n=7 fits exactly and n=8 overflows.
  TEST_ASSERT_EQUAL_STRING("{\"k\":\"xxxxxxx\"}",
                           serialize("{k = string.rep('x', 7)}", 16).c_str());
  TEST_ASSERT_EQUAL_STRING("<overflow>",
                           serialize("{k = string.rep('x', 8)}", 16).c_str());
}

// ── events.send integration: new payload shapes reach the envelope ────────

void test_events_send_rich_payload_through_envelope(void) {
  build();
  loadApp(
      "function init(ctx)\n"
      "  ok = events.send('rich', {ok = true, msg = 'say \"hi\"',\n"
      "                            nest = {a = 1}, xs = {1, 2}})\n"
      "end\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_EQUAL_INT(1, sinkCalls);
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok"));
  // The envelope re-serializes via ArduinoJson, so data survived a JSON
  // parse — proof the escaped payload was valid JSON.
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"msg\":\"say \\\"hi\\\"\""));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"nest\":{\"a\":1}"));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"xs\":[1,2]"));
}

void test_events_send_oversize_payload_dropped_not_truncated(void) {
  build();
  char code[512];
  snprintf(code, sizeof(code),
           "function init(ctx)\n"
           "  ok = events.send('big', {blob = string.rep('x', %d)})\n"
           "end\n"
           "function on_tick(ctx, dt) end\n",
           RESIDENT_EVENT_JSON_MAX + 100);
  loadApp(code);
  TEST_ASSERT_EQUAL_INT(0, sinkCalls);                       // nothing sent
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("ok"));    // send() -> false
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_events_send_builds_app_channel_envelope);
  RUN_TEST(test_publish_event_cpp_path);
  RUN_TEST(test_publish_event_rate_limited);
  RUN_TEST(test_nonces_are_unique_and_deviceid_prefixed);
  RUN_TEST(test_publish_event_without_sink_or_network_returns_false);
  RUN_TEST(test_serializer_flat_payloads_unchanged);
  RUN_TEST(test_serializer_escapes_quotes_backslashes_controls);
  RUN_TEST(test_serializer_escapes_keys);
  RUN_TEST(test_serializer_booleans);
  RUN_TEST(test_serializer_nested_objects_to_depth_3);
  RUN_TEST(test_serializer_nested_arrays);
  RUN_TEST(test_serializer_top_level_is_always_an_object);
  RUN_TEST(test_serializer_skips_unsupported_values);
  RUN_TEST(test_serializer_overflow_returns_false);
  RUN_TEST(test_events_send_rich_payload_through_envelope);
  RUN_TEST(test_events_send_oversize_payload_dropped_not_truncated);
  return UNITY_END();
}
