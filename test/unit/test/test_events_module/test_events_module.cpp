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

// Incoming mirror: parse `json` with ArduinoJson, push via
// Resident::pushJsonObjectToLua, and evaluate `predicate` — a Lua expression
// over `d`, the resulting data table.
bool parseCheck(const char* json, const char* predicate) {
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  JsonDocument doc;
  if (deserializeJson(doc, json)) { lua_close(L); return false; }
  std::string src = std::string("return function(d) return (") + predicate + ") end";
  if (luaL_dostring(L, src.c_str()) != LUA_OK) { lua_close(L); return false; }
  Resident::pushJsonObjectToLua(L, doc.as<JsonObjectConst>(), /*depth=*/1);
  bool ok = lua_pcall(L, 1, 1, 0) == LUA_OK && lua_toboolean(L, -1);
  lua_close(L);
  return ok;
}

// Round-trip: build tableExpr in Lua, serialize with the outgoing writer,
// parse back with the incoming reader, deep-compare against the original.
bool roundTrips(const char* tableExpr) {
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  luaL_dostring(L,
      "function deep_eq(a, b)\n"
      "  if type(a) ~= type(b) then return false end\n"
      "  if type(a) ~= 'table' then return a == b end\n"
      "  for k, v in pairs(a) do if not deep_eq(v, b[k]) then return false end end\n"
      "  for k in pairs(b) do if a[k] == nil then return false end end\n"
      "  return true\n"
      "end\n");
  std::string src = std::string("return ") + tableExpr;
  if (luaL_dostring(L, src.c_str()) != LUA_OK) { lua_close(L); return false; }
  char buf[512];
  if (!Resident::serializeLuaTableToJson(L, -1, buf, sizeof(buf))) { lua_close(L); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)buf)) { lua_close(L); return false; }
  lua_getglobal(L, "deep_eq");
  lua_pushvalue(L, -2);                                       // original table
  Resident::pushJsonObjectToLua(L, doc.as<JsonObjectConst>(), /*depth=*/1);
  bool ok = lua_pcall(L, 2, 1, 0) == LUA_OK && lua_toboolean(L, -1);
  lua_close(L);
  return ok;
}

void pump() { testMillis() += 200; sandbox->loop(); }

constexpr const char* COUNTING_APP =
    "cnt = 0\n"
    "function init(ctx) end\n"
    "function on_tick(ctx, dt) end\n"
    "function on_event(ctx, event) cnt = cnt + 1 end\n";
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

// ── incoming path: pushJsonObjectToLua unit tests (mirror the outgoing) ───

void test_parser_unescapes_strings(void) {
  TEST_ASSERT_TRUE(parseCheck(
      "{\"m\":\"say \\\"hi\\\"\",\"p\":\"a\\\\b\",\"nl\":\"l1\\nl2\\t.\\r\","
      "\"c\":\"a\\u0001b\"}",
      "d.m == 'say \"hi\"' and d.p == 'a\\\\b' and d.nl == 'l1\\nl2\\t.\\r' "
      "and d.c == 'a\\1b'"));
}

void test_parser_booleans_and_numbers(void) {
  TEST_ASSERT_TRUE(parseCheck(
      "{\"t\":true,\"f\":false,\"n\":1,\"x\":1.5,\"neg\":-7}",
      "d.t == true and d.f == false and d.n == 1 and "
      "math.type(d.n) == 'integer' and d.x == 1.5 and d.neg == -7"));
}

void test_parser_nested_objects_to_depth_3(void) {
  TEST_ASSERT_TRUE(parseCheck("{\"a\":{\"b\":{\"c\":1}}}", "d.a.b.c == 1"));
  // A container that would sit at depth 4 is skipped with its key,
  // mirroring the outgoing serializer.
  TEST_ASSERT_TRUE(parseCheck("{\"a\":{\"b\":{\"c\":{\"deep\":1}}}}",
                              "d.a.b.c == nil and d.a.b ~= nil"));
}

void test_parser_arrays(void) {
  TEST_ASSERT_TRUE(parseCheck(
      "{\"xs\":[1,true,\"a\"]}",
      "d.xs[1] == 1 and d.xs[2] == true and d.xs[3] == 'a' and #d.xs == 3"));
  // JSON null has no Lua value: a hole at its index, later elements keep
  // their positions (the incoming twin of the outgoing null placeholder).
  TEST_ASSERT_TRUE(parseCheck("{\"xs\":[1,null,3]}",
                              "d.xs[1] == 1 and d.xs[2] == nil and d.xs[3] == 3"));
}

void test_serializer_parser_round_trip(void) {
  TEST_ASSERT_TRUE(roundTrips("{n = 1, f = 1.5, neg = -7, tag = 'x'}"));
  TEST_ASSERT_TRUE(roundTrips(
      "{m = 'say \"hi\"', p = 'a\\\\b', nl = 'l1\\nl2\\t.\\r', c = 'a\\1b'}"));
  TEST_ASSERT_TRUE(roundTrips("{ok = true, off = false}"));
  TEST_ASSERT_TRUE(roundTrips("{a = {b = {c = 1}}, xs = {1, 'a', true}}"));
  TEST_ASSERT_TRUE(roundTrips("{}"));
}

// ── incoming path: integration through the sandbox ────────────────────────

// The live-bug repro: a server→device event whose string value contains
// double quotes (plus booleans, a nested object, and an array) must reach
// on_event intact.
void test_incoming_quoted_string_reaches_lua_intact(void) {
  build();
  loadApp(
      "ok = false\n"
      "cnt = 0\n"
      "function init(ctx) end\n"
      "function on_tick(ctx, dt) end\n"
      "function on_event(ctx, event)\n"
      "  cnt = cnt + 1\n"
      "  ok = event.data.msg == [[it's a \"good\" day]]\n"
      "       and event.data.on == true\n"
      "       and event.data.pos.x == 1\n"
      "       and event.data.xs[2] == 'b'\n"
      "end\n");
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "heard";
  evt["from"] = "otherdev";
  evt["data"]["msg"] = "it's a \"good\" day";
  evt["data"]["on"] = true;
  evt["data"]["pos"]["x"] = 1;
  evt["data"]["xs"][0] = "a";
  evt["data"]["xs"][1] = "b";
  sandbox->injectMessage("test", "heard", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok"));
}

void test_incoming_unparseable_data_drops_event(void) {
  build();
  loadApp(COUNTING_APP);
  sandbox->sendAppEvent("evt", "{\"broken");   // cut-off JSON via the C++ path
  pump();
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("cnt"));
  sandbox->sendAppEvent("evt", "{\"n\":1}");   // sanity: valid data delivers
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
}

void test_incoming_oversize_data_dropped(void) {
  build();
  loadApp(COUNTING_APP);
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "big";
  evt["from"] = "otherdev";
  std::string blob(RESIDENT_EVENT_JSON_MAX + 100, 'x');
  evt["data"]["blob"] = blob.c_str();
  sandbox->injectMessage("test", "big", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("cnt"));
}

// ── envelope: channel/src/seq through the sandbox boundary ────────────────

void test_outgoing_envelope_stamps_src_and_monotonic_seq(void) {
  build();
  sandbox->publishEvent("a", "{}");
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"channel\":\"app\""));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"src\":\"device\""));
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"seq\":1,"));
  sandbox->publishEvent("b", "{}");
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), "\"seq\":2,"));  // monotonic
  // seq and the nonce suffix stay in lockstep for a frame.
  char nonce[80];
  snprintf(nonce, sizeof(nonce), "\"nonce\":\"%s:2\"", sandbox->getDeviceId().c_str());
  TEST_ASSERT_NOT_NULL(strstr(captured.c_str(), nonce));
}

void test_incoming_app_frame_exposes_envelope_to_lua(void) {
  build();
  loadApp(
      "ok = false\n"
      "cnt = 0\n"
      "function init(ctx) end\n"
      "function on_tick(ctx, dt) end\n"
      "function on_event(ctx, event)\n"
      "  cnt = cnt + 1\n"
      "  ok = event.channel == 'app' and event.src == 'server'\n"
      "       and event.seq == 7 and event.data.n == 1\n"
      "end\n");
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "ping";
  evt["from"] = "otherdev";
  evt["src"] = "server";
  evt["seq"] = 7;
  evt["data"]["n"] = 1;
  sandbox->injectMessage("test", "ping", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok"));
}

void test_incoming_runtime_frame_routed_to_on_event(void) {
  build();
  loadApp(
      "ok = false\n"
      "cnt = 0\n"
      "function init(ctx) end\n"
      "function on_tick(ctx, dt) end\n"
      "function on_event(ctx, event)\n"
      "  cnt = cnt + 1\n"
      "  ok = event.channel == 'runtime' and event.name == 'heard'\n"
      "       and event.data.text == 'hi'\n"
      "end\n");
  JsonDocument evt;
  evt["channel"] = "runtime";
  evt["type"] = "heard";
  evt["from"] = "server";
  evt["data"]["text"] = "hi";
  sandbox->injectMessage("test", "heard", evt);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("cnt"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok"));
}

void test_host_injected_event_defaults_driver_channel(void) {
  build();
  loadApp(
      "saw_driver = false\n"
      "saw_runtime = false\n"
      "function init(ctx) end\n"
      "function on_tick(ctx, dt) end\n"
      "function on_event(ctx, event)\n"
      "  if event.channel == 'driver' and event.src == nil\n"
      "     and event.seq == nil then saw_driver = true end\n"
      "  if event.channel == 'runtime' then saw_runtime = true end\n"
      "end\n");
  sandbox->sendAppEvent("evt", "{}");            // default: "driver"
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("saw_driver"));
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("saw_runtime"));
  sandbox->sendAppEvent("evt", "{}", "runtime"); // caller-specified channel
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("saw_runtime"));
}

// ── ctx.generation_id ─────────────────────────────────────────────────────

void test_ctx_generation_id_from_wire_in_all_callbacks(void) {
  build();
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["generationId"] = "gen42";
  doc["code"] =
      "function init(ctx) init_ok = (ctx.generation_id == 'gen42') end\n"
      "function on_tick(ctx, dt) tick_ok = (ctx.generation_id == 'gen42') end\n"
      "function on_event(ctx, event) evt_ok = (ctx.generation_id == 'gen42') end\n";
  sandbox->injectMessage("test", "app", doc);
  pump();                                  // one on_tick
  sandbox->sendAppEvent("poke", "{}");
  pump();                                  // deliver to on_event
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("init_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("tick_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("evt_ok"));
}

void test_ctx_generation_id_nil_when_not_sent(void) {
  build();
  loadApp(
      "function init(ctx) init_nil = (ctx.generation_id == nil) end\n"
      "function on_tick(ctx, dt) tick_nil = (ctx.generation_id == nil) end\n"
      "function on_event(ctx, event) evt_nil = (ctx.generation_id == nil) end\n");
  pump();
  sandbox->sendAppEvent("poke", "{}");
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("init_nil"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("tick_nil"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("evt_nil"));
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
  RUN_TEST(test_parser_unescapes_strings);
  RUN_TEST(test_parser_booleans_and_numbers);
  RUN_TEST(test_parser_nested_objects_to_depth_3);
  RUN_TEST(test_parser_arrays);
  RUN_TEST(test_serializer_parser_round_trip);
  RUN_TEST(test_incoming_quoted_string_reaches_lua_intact);
  RUN_TEST(test_incoming_unparseable_data_drops_event);
  RUN_TEST(test_incoming_oversize_data_dropped);
  RUN_TEST(test_outgoing_envelope_stamps_src_and_monotonic_seq);
  RUN_TEST(test_incoming_app_frame_exposes_envelope_to_lua);
  RUN_TEST(test_incoming_runtime_frame_routed_to_on_event);
  RUN_TEST(test_host_injected_event_defaults_driver_channel);
  RUN_TEST(test_ctx_generation_id_from_wire_in_all_callbacks);
  RUN_TEST(test_ctx_generation_id_nil_when_not_sent);
  return UNITY_END();
}
