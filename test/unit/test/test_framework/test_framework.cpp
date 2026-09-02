// Framework module hosting (R16) + the execution deadline (R8).
//
// A framework module is privileged Lua hosted OUTSIDE the app: private
// environment (bare assignments stay framework-local; _G.x is the explicit
// app-facing install), the runtime-channel sender as a capability, and
// lifecycle interception. The framework slot persists over-the-wire
// updates; only {channel:"system", type:"framework"} can write it.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

// Built-in test framework: installs an API, mirrors private state into
// app-visible globals, consumes "secret" events, publishes on runtime.
constexpr const char* FRAMEWORK_V1 =
    "installs = 0\n"                       // private: framework env only
    "function framework_install()\n"
    "  installs = installs + 1\n"
    "  _G.api = { tag = 'v1', double = function(x) return x * 2 end }\n"
    "  _G.fw_installs = installs\n"
    "end\n"
    "function framework_tick(ctx, dt)\n"
    "  ticks = (ticks or 0) + 1\n"
    "  _G.fw_ticks = ticks\n"
    "end\n"
    "function framework_event(ctx, e)\n"
    "  if e.name == 'secret' then\n"
    "    runtime.send('seen_secret', { text = e.data.text })\n"
    "    return true\n"
    "  end\n"
    "  return false\n"
    "end\n"
    "function framework_app_loaded()\n"
    "  _G.fw_loaded = true\n"
    "end\n";

class FakeStore : public Resident::PersistentStore {
public:
  String app;
  String framework;
  bool begin() override { return true; }
  bool save(const char* s, size_t l) override { app = String(std::string(s, l).c_str()); return true; }
  String load() override { return app; }
  void clear() override { app = String(); }
  bool saveFramework(const char* s, size_t l) override {
    framework = String(std::string(s, l).c_str());
    return true;
  }
  String loadFramework() override { return framework; }
  void clearFramework() override { framework = String(); }
};

Resident::Sandbox* sandbox = nullptr;
FakeStore* store = nullptr;
std::vector<std::string>* events = nullptr;
std::vector<std::string>* frames = nullptr;
std::vector<std::string>* telemetry = nullptr;

void build(const char* fwSource = FRAMEWORK_V1, uint32_t deadlineMs = 0) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.persistentStore = store;
  cfg.executionDeadlineMs = deadlineMs;
  if (fwSource) {
    Resident::SandboxConfig::FrameworkConfig fw;
    fw.name = "testfw";
    fw.version = 1;
    fw.source = fwSource;
    cfg.framework = fw;
  }
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setTelemetryCallback([](const char* json) {
    telemetry->push_back(json ? json : "");
  });
  sandbox->setEventSink([](JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    events->push_back(out);
    return true;
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

void injectAppEvent(const char* name, const char* text = nullptr) {
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = name;
  JsonObject d = evt["data"].to<JsonObject>();
  if (text) d["text"] = text;
  sandbox->injectMessage("test", name, evt);
}

bool telemetryHas(const char* name) {
  for (auto& t : *telemetry)
    if (t.find(std::string("\"name\":\"") + name + "\"") != std::string::npos) return true;
  return false;
}

const std::string* eventWith(const char* needle) {
  for (auto& e : *events)
    if (e.find(needle) != std::string::npos) return &e;
  return nullptr;
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
  store = new FakeStore();
  events = new std::vector<std::string>();
  frames = new std::vector<std::string>();
  telemetry = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete store; store = nullptr;
  delete events; events = nullptr;
  delete frames; frames = nullptr;
  delete telemetry; telemetry = nullptr;
}

void test_framework_hosts_lifecycle_and_installs_api(void) {
  build();
  TEST_ASSERT_TRUE(telemetryHas("framework_applied"));
  // An app with NO lifecycle globals loads fine under a framework, sees
  // the installed API, and the app_loaded hook fired.
  loadApp(
      "has_api = (api ~= nil and api.tag == 'v1' and api.double(21) == 42)\n"
      "private_hidden = (installs == nil) and (ticks == nil)\n"
      "  and (runtime == nil)\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("has_api"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("fw_loaded"));
  // Framework internals AND the runtime capability are unreachable.
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("private_hidden"));
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("fw_installs"));
  // Reinstall on the next load — fresh env, same framework.
  loadApp("x = 1\n");
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("fw_installs"));
}

void test_framework_tick_runs_before_app_tick(void) {
  build();
  loadApp("function on_tick(ctx, dt) app_saw = fw_ticks end\n");
  pump();
  pump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("fw_ticks"));
  // The framework ticked before the app in the same loop pass.
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("app_saw"));
}

void test_framework_event_consumes_and_publishes_on_runtime_channel(void) {
  build();
  loadApp(
      "seen = 0\n"
      "function on_event(ctx, e) seen = seen + 1 end\n");
  injectAppEvent("secret", "shh");
  injectAppEvent("plain");
  pump();
  pump();
  // The framework consumed "secret" (app never saw it) and published on
  // the runtime channel; "plain" reached the app.
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("seen"));
  const std::string* f = eventWith("\"type\":\"seen_secret\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"channel\":\"runtime\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"text\":\"shh\"") != std::string::npos);
}

void test_framework_receives_events_without_app_handler(void) {
  build();
  loadApp("x = 1\n");   // no on_event at all
  injectAppEvent("secret", "still heard");
  pump();
  TEST_ASSERT_NOT_NULL(eventWith("\"type\":\"seen_secret\""));
}

void test_framework_slot_update_applies_persists_and_survives_reboot(void) {
  build();
  loadApp("x = 1\n");
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "framework";
  doc["name"] = "testfw";
  doc["version"] = 2;
  doc["code"] =
      "function framework_install() _G.api = { tag = 'v2' } end\n";
  sandbox->injectMessage("test", "framework", doc);
  // Applied live: the next app load installs v2's API.
  loadApp("tag = api.tag\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("tag"));
  // Hello announces the slot version.
  sandbox->requestHello();
  pump();
  const std::string* h = frameWith("\"type\":\"hello\"");
  TEST_ASSERT_NOT_NULL(h);
  TEST_ASSERT_TRUE(h->find("\"version\":2") != std::string::npos);
  TEST_ASSERT_TRUE(h->find("\"source\":\"slot\"") != std::string::npos);
  // Persisted: a fresh sandbox on the same store boots the slot copy.
  TEST_ASSERT_TRUE(store->framework.length() > 0);
  delete sandbox;
  sandbox = nullptr;
  build();
  loadApp("tag2 = (api.tag == 'v2')\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("tag2"));
}

void test_framework_bad_slot_code_keeps_current(void) {
  build();
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "framework";
  doc["code"] = "this is not lua(";
  sandbox->injectMessage("test", "framework", doc);
  TEST_ASSERT_TRUE(telemetryHas("framework_error"));
  loadApp("still_v1 = (api.tag == 'v1')\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("still_v1"));
}

void test_framework_empty_code_reverts_to_builtin(void) {
  build();
  // Install a slot version, then revert.
  JsonDocument up;
  up["channel"] = "system";
  up["type"] = "framework";
  up["version"] = 2;
  up["code"] = "function framework_install() _G.api = { tag = 'v2' } end\n";
  sandbox->injectMessage("test", "framework", up);
  JsonDocument down;
  down["channel"] = "system";
  down["type"] = "framework";
  down["code"] = "";
  sandbox->injectMessage("test", "framework", down);
  TEST_ASSERT_EQUAL_INT(0, (int)store->framework.length());   // slot cleared
  loadApp("back = (api.tag == 'v1')\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("back"));
}

void test_execution_deadline_kills_the_dispatch_not_the_device(void) {
  build(FRAMEWORK_V1, /*deadlineMs=*/50);
  loadApp(
      "ticks = 0\n"
      "function on_tick(ctx, dt)\n"
      "  ticks = ticks + 1\n"
      "  if ticks == 2 then local a = 0 while true do a = a + 1 end end\n"
      "end\n");
  pump();                 // tick 1: fine
  pump();                 // tick 2: runaway — aborted at the deadline
  TEST_ASSERT_TRUE(telemetryHas("runtime_error"));
  bool deadlineMsg = false;
  for (auto& t : *telemetry)
    if (t.find("execution deadline exceeded") != std::string::npos) deadlineMsg = true;
  TEST_ASSERT_TRUE(deadlineMsg);
  pump();                 // tick 3: the app is still alive
  TEST_ASSERT_EQUAL_INT(3, sandbox->luaGlobalIntForTest("ticks"));
}

// The hook the timer installs is one-shot and re-checks the deadline, so a
// dispatch that keeps to its deadline is never touched — including the one
// immediately after an abort.
void test_execution_deadline_leaves_an_in_time_dispatch_alone(void) {
  build(FRAMEWORK_V1, /*deadlineMs=*/2000);
  loadApp(
      "ticks = 0\n"
      "function on_tick(ctx, dt)\n"
      "  ticks = ticks + 1\n"
      "  local a = 0\n"
      "  for i = 1, 20000 do a = a + i end\n"
      "end\n");
  for (int i = 0; i < 5; i++) pump();
  TEST_ASSERT_EQUAL_INT(5, sandbox->luaGlobalIntForTest("ticks"));
  TEST_ASSERT_FALSE(telemetryHas("runtime_error"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_framework_hosts_lifecycle_and_installs_api);
  RUN_TEST(test_framework_tick_runs_before_app_tick);
  RUN_TEST(test_framework_event_consumes_and_publishes_on_runtime_channel);
  RUN_TEST(test_framework_receives_events_without_app_handler);
  RUN_TEST(test_framework_slot_update_applies_persists_and_survives_reboot);
  RUN_TEST(test_framework_bad_slot_code_keeps_current);
  RUN_TEST(test_framework_empty_code_reverts_to_builtin);
  RUN_TEST(test_execution_deadline_kills_the_dispatch_not_the_device);
  RUN_TEST(test_execution_deadline_leaves_an_in_time_dispatch_alone);
  return UNITY_END();
}
