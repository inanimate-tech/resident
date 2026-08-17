// Sandbox hardening (0.8): offline-first ticking, the closed stdlib,
// store budget feedback, drop accounting, and the hello-gated closure of
// the legacy message paths.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* frames = nullptr;
std::vector<std::string>* telemetry = nullptr;

void build(bool networked = false, bool gateTick = false, bool openLibs = false) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.gateTickOnConnection = gateTick;
  cfg.openUnsafeLibs = openLibs;
  if (networked) {
    Courier::Config net;
    net.host = "example.invalid";
    cfg.network = net;
  }
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setTelemetryCallback([](const char* json) {
    telemetry->push_back(json ? json : "");
  });
  sandbox->setSystemSink([](JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    frames->push_back(out);
    return true;
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

void injectAppEvent(const char* name) {
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = name;
  evt["data"].to<JsonObject>();
  sandbox->injectMessage("test", name, evt);
}

void hostHello() {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "hello";
  doc["data"]["proto"] = 1;
  sandbox->injectMessage("test", "hello", doc);
}

int telemetryCount(const char* name) {
  int n = 0;
  for (auto& t : *telemetry)
    if (t.find(std::string("\"name\":\"") + name + "\"") != std::string::npos) n++;
  return n;
}

const std::string* frameWith(const char* needle) {
  for (auto& f : *frames)
    if (f.find(needle) != std::string::npos) return &f;
  return nullptr;
}

void pump() { testMillis() += 200; sandbox->loop(); }

}  // namespace

void setUp(void) {
  testMillis() = 0;
  frames = new std::vector<std::string>();
  telemetry = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete frames; frames = nullptr;
  delete telemetry; telemetry = nullptr;
}

void test_disconnected_device_still_ticks_and_dispatches(void) {
  // Networked but never connected (the stub's state is Idle): the app must
  // tick and receive queued events anyway — offline-first.
  build(/*networked=*/true);
  loadApp(
      "ticks = 0\n"
      "events = 0\n"
      "function on_tick(ctx, dt) ticks = ticks + 1 end\n"
      "function on_event(ctx, e) events = events + 1 end\n");
  injectAppEvent("ping");
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("ticks"));
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("events"));
}

void test_gate_tick_on_connection_restores_old_behavior(void) {
  build(/*networked=*/true, /*gateTick=*/true);
  loadApp("ticks = 0\nfunction on_tick(ctx, dt) ticks = ticks + 1 end\n");
  pump();
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("ticks"));
}

void test_unsafe_stdlib_closed_by_default(void) {
  build();
  loadApp(
      "closed = (os == nil) and (io == nil) and (package == nil)\n"
      "  and (require == nil) and (dofile == nil) and (load == nil)\n"
      "  and (loadstring == nil) and (debug == nil)\n"
      "safe = (string ~= nil) and (table ~= nil) and (math ~= nil)\n"
      "  and (coroutine ~= nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("closed"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("safe"));
}

void test_open_unsafe_libs_opts_back_in(void) {
  build(false, false, /*openLibs=*/true);
  loadApp(
      "open = (os ~= nil) and (io ~= nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("open"));
}

void test_store_remaining_and_store_full_once_per_key(void) {
  build();
  loadApp(
      "r0 = store.remaining()\n"
      "store.set('k', 'hello')\n"
      "r1 = store.remaining()\n"
      "big = string.rep('x', 3000)\n"
      "ok1 = store.set('huge', big)\n"
      "ok2 = store.set('huge', big)\n"   // same key: one report
      "ok3 = store.set('huge2', big)\n"  // new key: second report
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalIntForTest("r0") > sandbox->luaGlobalIntForTest("r1"));
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("ok1"));
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("ok3"));
  TEST_ASSERT_EQUAL_INT(2, telemetryCount("store_full"));
}

void test_ring_overflow_reports_dropped_count(void) {
  build();
  loadApp("seen = 0\nfunction on_event(ctx, e) seen = seen + 1 end\n");
  for (int i = 0; i < 12; i++) injectAppEvent("burst");
  for (int i = 0; i < 12; i++) pump();
  // 8-slot ring, 7 usable: 5 of 12 dropped.
  TEST_ASSERT_EQUAL_INT(7, sandbox->luaGlobalIntForTest("seen"));
  frames->clear();
  testMillis() += 61000;
  sandbox->loop();
  const std::string* f = frameWith("\"name\":\"dropped\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"count\":5") != std::string::npos);
}

void test_legacy_paths_close_after_host_hello(void) {
  build();
  loadApp("seen = 0\nfunction on_event(ctx, e) seen = seen + 1 end\n");

  // Pre-hello: the legacy app_event wrapper still delivers.
  JsonDocument legacy;
  legacy["channel"] = "app";
  legacy["type"] = "app_event";
  legacy["name"] = "old";
  legacy["data"].to<JsonObject>();
  sandbox->injectMessage("test", "app_event", legacy);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("seen"));

  // Post-hello: both legacy shapes drop (and count).
  hostHello();
  JsonDocument legacy2;
  legacy2["channel"] = "app";
  legacy2["type"] = "app_event";
  legacy2["name"] = "old";
  legacy2["data"].to<JsonObject>();
  sandbox->injectMessage("test", "app_event", legacy2);
  JsonDocument unchannelled;
  unchannelled["type"] = "app_event";
  unchannelled["name"] = "old";
  unchannelled["data"].to<JsonObject>();
  sandbox->injectMessage("test", "app_event", unchannelled);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("seen"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_disconnected_device_still_ticks_and_dispatches);
  RUN_TEST(test_gate_tick_on_connection_restores_old_behavior);
  RUN_TEST(test_unsafe_stdlib_closed_by_default);
  RUN_TEST(test_open_unsafe_libs_opts_back_in);
  RUN_TEST(test_store_remaining_and_store_full_once_per_key);
  RUN_TEST(test_ring_overflow_reports_dropped_count);
  RUN_TEST(test_legacy_paths_close_after_host_hello);
  return UNITY_END();
}
