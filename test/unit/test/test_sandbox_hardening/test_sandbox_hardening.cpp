// Sandbox hardening (0.8): offline-first ticking, the closed stdlib,
// store budget feedback, drop accounting, and the hello-gated closure of
// the legacy message paths. Also the app's narrowed base functions
// (ResidentAppBaseLib.h): no __gc, no userdata metatables, no lasting
// collector changes.
#include <unity.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

Resident::Sandbox* sandbox = nullptr;

// A binding-style userdata: a shared metatable with a native __gc, like a
// graphics library's object handles. probe.make() returns one.
int probeFinalized = 0;

int probeGc(lua_State*) {
  probeFinalized++;
  return 0;
}

int probeMake(lua_State* L) {
  lua_newuserdatauv(L, 1, 0);
  if (luaL_newmetatable(L, "native_test_probe")) {
    lua_pushcfunction(L, probeGc);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
  return 1;
}

class Probe : public Resident::Extension {
 public:
  const char* name() const override { return "probe"; }
  void registerModule(Resident::LuaModule& m) override { m.staticMethod("make", probeMake); }
};

Probe probe;
std::vector<std::string>* frames = nullptr;
std::vector<std::string>* telemetry = nullptr;

void build(bool networked = false, bool gateTick = false, bool openLibs = false) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {&probe};
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
  doc["data"]["protocol"] = 1;
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
  probeFinalized = 0;
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

void test_fresh_environment_clears_app_globals(void) {
  build();
  loadApp(
      "leak = 42\n"
      "function helper() return 1 end\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_EQUAL_INT(42, sandbox->luaGlobalIntForTest("leak"));
  loadApp(
      "gone = (leak == nil) and (helper == nil)\n"
      "baseline_ok = (events ~= nil) and (store ~= nil) and (log ~= nil)\n"
      "  and (time ~= nil) and (string ~= nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("gone"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("baseline_ok"));
}

void test_fresh_environment_flag_off_keeps_leakage(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.freshAppEnvironment = false;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  loadApp("leak = 42\nfunction on_tick(ctx, dt) end\n");
  loadApp("kept = (leak == 42)\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("kept"));
}

void test_chunks_keep_the_running_environment(void) {
  build();
  loadApp("g = 1\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->loadChunk("g = g + 1"));
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("g"));
  loadApp("fresh = (g == nil)\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("fresh"));
}

void test_setmetatable_refuses_a_gc_metatable(void) {
  build();
  loadApp(
      "local ok, err = pcall(setmetatable, {}, { __gc = function() end })\n"
      "refused = (not ok) and (string.find(err, '__gc', 1, true) ~= nil)\n"
      "refused_false = not pcall(setmetatable, {}, { __gc = false })\n"
      "plain = getmetatable(setmetatable({}, { k = 1 })).k == 1\n"
      "cleared = getmetatable(setmetatable(setmetatable({}, {}), nil)) == nil\n"
      "local locked = setmetatable({}, { __metatable = 'locked' })\n"
      "protected = (getmetatable(locked) == 'locked')\n"
      "  and not pcall(setmetatable, locked, {})\n"
      // __gc added after the metatable is attached never registers a
      // finalizer: Lua checks for it only at setmetatable.
      "local mt = {}\n"
      "local t = setmetatable({}, mt)\n"
      "mt.__gc = function() late_ran = true end\n"
      "t = nil\n"
      "collectgarbage(); collectgarbage()\n"
      "late = (late_ran == nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("refused"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("refused_false"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("plain"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("cleared"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("protected"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("late"));
}

void test_getmetatable_returns_nil_for_userdata(void) {
  build();
  loadApp(
      "u = probe.make()\n"
      "hidden = (type(u) == 'userdata') and (getmetatable(u) == nil)\n"
      "tables = getmetatable(setmetatable({}, { k = 1 })).k == 1\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("hidden"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("tables"));
  // The native finalizer is the binding's own, and still runs: the next
  // app's reset collects the userdata.
  loadApp("function on_tick(ctx, dt) end\n");
  TEST_ASSERT_EQUAL_INT(1, probeFinalized);
}

void test_collectgarbage_keeps_the_collector_as_it_was(void) {
  build();
  loadApp(
      "count_ok = type(collectgarbage('count')) == 'number'\n"
      "step_ok = type(collectgarbage('step')) == 'boolean'\n"
      "running = collectgarbage('isrunning')\n"
      "collect_ok = (collectgarbage() == 0) and (collectgarbage('collect') == 0)\n"
      "local refused = 0\n"
      "for _, o in ipairs({ 'stop', 'restart', 'setpause', 'setstepmul',\n"
      "                     'incremental', 'generational', 'bogus' }) do\n"
      "  local ok, err = pcall(collectgarbage, o, 1)\n"
      "  if not ok and string.find(err, o, 1, true) then refused = refused + 1 end\n"
      "end\n"
      "refused_all = (refused == 7)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("count_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("step_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("running"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("collect_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("refused_all"));
  loadApp("still_running = collectgarbage('isrunning')\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("still_running"));
}

void test_hostile_finalizer_app_then_next_app_completes(void) {
  // A finalizer runs with hooks off, out of the deadline's reach: had any
  // of these routes registered one, the next load's reset GC would spin
  // for ever. SIGALRM's default action ends the run, failing the suite.
  alarm(10);
  build();
  loadApp(
      "local spin = function() while true do end end\n"
      "pcall(setmetatable, {}, { __gc = spin })\n"
      "local mt = {}\n"
      "keep = setmetatable({}, mt)\n"
      "mt.__gc = spin\n"
      "pcall(function() getmetatable(probe.make()).__gc = spin end)\n"
      "pcall(collectgarbage, 'stop')\n"
      "function on_tick(ctx, dt) end\n");
  pump();
  loadApp("next_ok = collectgarbage('isrunning')\nfunction on_tick(ctx, dt) end\n");
  pump();
  alarm(0);
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("next_ok"));
  TEST_ASSERT_EQUAL_INT(1, probeFinalized);  // its own native __gc, untouched
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
  RUN_TEST(test_fresh_environment_clears_app_globals);
  RUN_TEST(test_fresh_environment_flag_off_keeps_leakage);
  RUN_TEST(test_chunks_keep_the_running_environment);
  RUN_TEST(test_setmetatable_refuses_a_gc_metatable);
  RUN_TEST(test_getmetatable_returns_nil_for_userdata);
  RUN_TEST(test_collectgarbage_keeps_the_collector_as_it_was);
  RUN_TEST(test_hostile_finalizer_app_then_next_app_completes);
  return UNITY_END();
}
