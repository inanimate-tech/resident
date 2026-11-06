// GuardedDispatch: the execution guard as an extension holds it, around its
// own C code that calls app Lua through lua_pcall (a library's callbacks,
// run from update()). test_lvgl_pump runs the same guard over the real LVGL
// pump; this suite pins the contract any extension relies on.
#include <unity.h>
#include <chrono>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

constexpr uint32_t kDeadlineMs = 50;
constexpr long kLimitMs = 750;

#define SPIN "for i = 1, 100000000 do end\n"

// Calls the app's global functions `cb1` and `cb2` from update(), each
// through its own lua_pcall as a library's callback would, inside guarded
// scopes shaped by `nested`.
class CallbackExtension : public Resident::Extension {
public:
  const char* name() const override { return "callbacks"; }
  void registerModule(Resident::LuaModule& m) override { _lua = m.state(); }
  void setDispatchGuard(Resident::DispatchGuard* guard) override {
    _guard = guard;
    if (!_begun) guardBeforeBegin = guard != nullptr;   // the first sandbox's
  }
  void begin() override { _begun = true; }
  void update() override {
    if (!enabled) return;
    if (nested) {
      // An inner scope joins the outer one: one deadline, one report.
      Resident::GuardedDispatch outer(_guard, "outer");
      {
        Resident::GuardedDispatch inner(_guard, "inner");
        call("cb1");
      }
      call("cb2");
    } else {
      Resident::GuardedDispatch dispatch(useNullGuard ? nullptr : _guard, "callbacks");
      call("cb1");
      call("cb2");
    }
  }

  bool enabled = false;
  bool nested = false;
  bool useNullGuard = false;
  bool guardBeforeBegin = false;

private:
  void call(const char* fn) {
    lua_getglobal(_lua, fn);
    if (!lua_isfunction(_lua, -1)) {
      lua_pop(_lua, 1);
      return;
    }
    if (lua_pcall(_lua, 0, 0, 0) != LUA_OK) lua_pop(_lua, 1);
  }

  lua_State* _lua = nullptr;
  Resident::DispatchGuard* _guard = nullptr;
  bool _begun = false;
};

CallbackExtension callbacks;
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.executionDeadlineMs = kDeadlineMs;
  cfg.extensions = {&callbacks};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setTelemetryCallback([](const char* json) {
    telemetry->push_back(json ? json : "");
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

int telemetryCount(const char* needle) {
  int n = 0;
  for (auto& t : *telemetry)
    if (t.find(needle) != std::string::npos) n++;
  return n;
}

long timedPass() {
  testMillis() += 200;
  const auto t0 = std::chrono::steady_clock::now();
  sandbox->loop();
  return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0)
      .count();
}

constexpr const char* kTwoRunaways =
    "a, b = 0, 0\n"
    "function cb1() a = 1; for n = 1, 3 do pcall(function() " SPIN " end) end; a = 2 end\n"
    "function cb2() b = 1; " SPIN " b = 2 end\n"
    "function on_tick() end\n";

}  // namespace

void setUp(void) {
  testMillis() = 0;
  telemetry = new std::vector<std::string>();
  callbacks.enabled = false;
  callbacks.nested = false;
  callbacks.useNullGuard = false;
  build();
}

void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  delete telemetry;
  telemetry = nullptr;
}

void test_extension_gets_the_guard_before_begin(void) {
  TEST_ASSERT_TRUE(callbacks.guardBeforeBegin);
}

// Both callbacks share the scope's one deadline: the first is stopped
// however it catches the error, the second at its first instruction. The
// scope is reported once, naming its source.
void test_runaways_in_one_scope_are_stopped_and_reported_once(void) {
  loadApp(kTwoRunaways);
  callbacks.enabled = true;
  TEST_ASSERT_LESS_THAN_INT32(kLimitMs, timedPass());
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("a"));
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("b"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("\"name\":\"runtime_error\""));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("callbacks: execution deadline exceeded (50 ms)"));
}

void test_nested_scopes_share_one_deadline(void) {
  loadApp(kTwoRunaways);
  callbacks.enabled = true;
  callbacks.nested = true;
  TEST_ASSERT_LESS_THAN_INT32(kLimitMs, timedPass());
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("b"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("\"name\":\"runtime_error\""));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("outer: execution deadline exceeded"));
}

// The guard counts the scope against the app as it does an aborted tick:
// three in 30 s suspend it.
void test_aborted_scopes_count_toward_throttling(void) {
  loadApp(kTwoRunaways);
  callbacks.enabled = true;
  for (int i = 0; i < 3; i++) timedPass();
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("\"name\":\"app_throttled\""));
}

// A scope whose Lua keeps to the deadline reports nothing.
void test_in_time_scope_reports_nothing(void) {
  loadApp(
      "n = 0\n"
      "function cb1() n = n + 1 end\n"
      "function on_tick() end\n");
  callbacks.enabled = true;
  for (int i = 0; i < 3; i++) timedPass();
  TEST_ASSERT_EQUAL_INT(3, sandbox->luaGlobalIntForTest("n"));
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("runtime_error"));
}

// A null guard (an extension used outside a sandbox) guards nothing: the
// callback runs out its loop. The loop here is short, so this stays quick.
void test_null_guard_is_inert(void) {
  loadApp(
      "function cb1() for i = 1, 1000000 do end; done = true end\n"
      "function on_tick() end\n");
  callbacks.enabled = true;
  callbacks.useNullGuard = true;
  timedPass();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("done"));
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("runtime_error"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_extension_gets_the_guard_before_begin);
  RUN_TEST(test_runaways_in_one_scope_are_stopped_and_reported_once);
  RUN_TEST(test_nested_scopes_share_one_deadline);
  RUN_TEST(test_aborted_scopes_count_toward_throttling);
  RUN_TEST(test_in_time_scope_reports_nothing);
  RUN_TEST(test_null_guard_is_inert);
  return UNITY_END();
}
