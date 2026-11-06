// The app's environment (ResidentAppEnv.h): each app runs in a fresh _ENV
// built from the pristine globals at load, so nothing one app does to a
// global — defining one, replacing a library function, rewriting a module —
// reaches the next app or the framework; the string metatable and the
// module metatables are locked; and a driver puts an app global in the
// running app's environment with Resident::setAppGlobal.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* events = nullptr;

// A module like a graphics binding's: a function, a table of constants, and
// a metatable whose __index falls through to a table owned elsewhere.
int fallthroughIndex(lua_State* L) {
  if (strcmp(luaL_checkstring(L, 2), "deep") == 0) {
    lua_pushinteger(L, 7);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int widgetPing(lua_State* L) {
  lua_pushstring(L, "pong");
  return 1;
}

class Widget : public Resident::Extension {
 public:
  const char* name() const override { return "widget"; }
  void registerModule(Resident::LuaModule& m) override {
    m.staticMethod("ping", widgetPing);
    lua_State* L = m.state();
    lua_createtable(L, 0, 1);
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "ONE");
    lua_setfield(L, -2, "CONST");
    m.fallthrough(fallthroughIndex);
  }
};

// A driver that gives the app a global, as a widget installer or a sensor
// driver does: beacon.install() puts `beacon_value` in the app's
// environment; lastSet says whether there was one to put it in.
class Beacon : public Resident::Extension {
 public:
  const char* name() const override { return "beacon"; }
  void registerModule(Resident::LuaModule& m) override {
    L_ = m.state();
    m.method<Beacon, &Beacon::install>("install");
  }
  int install(lua_State* L) {
    lua_pushinteger(L, 42);
    lastSet = Resident::setAppGlobal(L, "beacon_value");
    return 0;
  }
  lua_State* L_ = nullptr;
  bool lastSet = false;
};

Widget widget;
Beacon beacon;

// A framework that reads the pristine globals: it counts the event's data
// fields with pairs, formats with string.format and a method call it
// does NOT make (s_format is captured), and reads widget.ping.
constexpr const char* kFramework =
    "local s_format = string.format\n"
    "function framework_install(env) env.fw_api = { ok = true } end\n"
    "function framework_event(ctx, e)\n"
    "  if e.name ~= 'probe' then return false end\n"
    "  local n = 0\n"
    "  for k in pairs(e.data) do n = n + 1 end\n"
    "  runtime.send('probe_seen', { n = n, f = s_format('%d', n),\n"
    "    g = string.format('%d', n), up = string.upper('ok'),\n"
    "    w = widget.ping(), t = type(tostring) })\n"
    "  return true\n"
    "end\n";

void build(bool fresh = true, bool withFramework = false) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {&widget, &beacon};
  cfg.persistApps = false;
  cfg.freshAppEnvironment = fresh;
  if (withFramework) {
    Resident::SandboxConfig::FrameworkConfig fw;
    fw.name = "testfw";
    fw.version = 1;
    fw.source = kFramework;
    cfg.framework = fw;
  }
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setEventSink([](JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    events->push_back(out);
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

void pump() {
  testMillis() += 200;
  sandbox->loop();
}

bool flag(const char* name) { return sandbox->luaGlobalBoolForTest(name); }

}  // namespace

void setUp(void) {
  testMillis() = 0;
  events = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  delete events;
  events = nullptr;
}

void test_replaced_base_function_does_not_reach_the_next_app(void) {
  build();
  loadApp(
      "pairs = function() return function() return nil end end\n"
      "ipairs = nil\n"
      "tostring = function() return 'forged' end\n"
      "function on_tick(ctx, dt) end\n");
  loadApp(
      "local n = 0\n"
      "for _ in pairs({ 1, 2, 3 }) do n = n + 1 end\n"
      "pairs_ok = (n == 3) and (ipairs ~= nil) and (tostring(1) == '1')\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("pairs_ok"));
}

void test_replaced_library_function_does_not_reach_the_next_app(void) {
  build();
  loadApp(
      "string.format = function() return 'forged' end\n"
      "string.rep = nil\n"
      "math.pi = 3\n"
      "table.insert = function() end\n"
      "function on_tick(ctx, dt) end\n");
  loadApp(
      "lib_ok = (string.format('%d', 5) == '5') and (('%d'):format(5) == '5')\n"
      "  and (string.rep ~= nil) and (math.pi > 3.14)\n"
      "local t = {}\n"
      "table.insert(t, 1)\n"
      "lib_ok = lib_ok and (#t == 1)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("lib_ok"));
}

void test_replaced_module_function_does_not_reach_the_next_app(void) {
  build();
  loadApp(
      "widget.ping = function() return 'forged' end\n"
      "widget.CONST.ONE = 99\n"
      "log.info = function() error('forged') end\n"
      "time.ticks_ms = function() return -1 end\n"
      "events.send = nil\n"
      "function on_tick(ctx, dt) end\n");
  loadApp(
      "mod_ok = (widget.ping() == 'pong') and (widget.CONST.ONE == 1)\n"
      "  and (widget.deep == 7) and (time.ticks_ms() >= 0)\n"
      "  and (type(events.send) == 'function')\n"
      "log.info('fine')\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("mod_ok"));
}

void test_an_app_sees_its_own_changes(void) {
  build();
  // An app that extends a library sees its extension, through the method
  // syntax too: the string metatable reads the running app's `string`.
  loadApp(
      "function string.shout(s) return s:upper() .. '!' end\n"
      "math.tau = 2 * math.pi\n"
      "_G.via_g = 1\n"
      "own_ok = (('hi'):shout() == 'HI!') and (math.tau > 6)\n"
      "  and (via_g == 1) and (_G == _ENV) and (_G._G == _G)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("own_ok"));
  loadApp(
      "gone = (string.shout == nil) and (('hi').shout == nil)\n"
      "  and (math.tau == nil) and (via_g == nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("gone"));
}

void test_shared_metatables_are_locked(void) {
  build();
  loadApp(
      "string_locked = (getmetatable('') == false)\n"
      "module_locked = (getmetatable(widget) == false)\n"
      "  and not pcall(setmetatable, widget, {})\n"
      // An app's own tables keep the stock behaviour.
      "own_free = getmetatable(setmetatable({}, { k = 1 })).k == 1\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("string_locked"));
  TEST_ASSERT_TRUE(flag("module_locked"));
  TEST_ASSERT_TRUE(flag("own_free"));
}

void test_datetime_is_fresh_for_each_app(void) {
  build();
  loadApp(
      "datetime.extra = 1\n"
      "dt_ok = (datetime.timedelta ~= nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("dt_ok"));
  loadApp(
      "dt_fresh = (rawget(datetime, 'extra') == nil) and (datetime.date ~= nil)\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("dt_fresh"));
}

void test_chunk_runs_in_the_app_environment(void) {
  build();
  loadApp("n = 1\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->loadChunk("n = n + 1\nstring.extra = true"));
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("n"));
  loadApp("chunk_gone = (n == nil) and (string.extra == nil)\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("chunk_gone"));
}

void test_flag_off_keeps_the_previous_environment(void) {
  build(/*fresh=*/false);
  loadApp("string.kept = true\nleft = 1\nfunction on_tick(ctx, dt) end\n");
  loadApp("carried = (string.kept == true) and (left == 1)\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("carried"));
}

void test_set_app_global_reaches_the_running_app_only(void) {
  build();
  // No app yet: there is no environment to put it in.
  lua_pushinteger(beacon.L_, 1);
  TEST_ASSERT_FALSE(Resident::setAppGlobal(beacon.L_, "beacon_value"));
  TEST_ASSERT_EQUAL_INT(0, lua_gettop(beacon.L_));

  loadApp(
      "beacon.install()\n"
      "seen = beacon_value\n"
      "function on_tick(ctx, dt) end\n"
      // Raw: a metatable the app gives its environment is never consulted.
      "setmetatable(_G, { __newindex = function() error('ran') end,\n"
      "                   __index = function() error('ran') end })\n");
  TEST_ASSERT_TRUE(beacon.lastSet);
  TEST_ASSERT_EQUAL_INT(42, sandbox->luaGlobalIntForTest("seen"));

  lua_pushinteger(beacon.L_, 5);
  TEST_ASSERT_TRUE(Resident::setAppGlobal(beacon.L_, "late"));
  TEST_ASSERT_EQUAL(LUA_TNUMBER, Resident::getAppGlobal(beacon.L_, "late"));
  TEST_ASSERT_EQUAL_INT(5, (int)lua_tointeger(beacon.L_, -1));
  lua_pop(beacon.L_, 1);
  TEST_ASSERT_EQUAL_INT(0, lua_gettop(beacon.L_));

  // Not in the pristine globals, so not in the next app.
  loadApp("absent = (beacon_value == nil) and (late == nil)\nfunction on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(flag("absent"));
}

void test_framework_reads_pristine_globals(void) {
  build(/*fresh=*/true, /*withFramework=*/true);
  // The app poisons everything the framework touches, in every way it can
  // reach: its globals, its library copies, the shared string metatable.
  loadApp(
      "api_seen = (fw_api ~= nil and fw_api.ok)\n"
      "pairs = function() return function() return nil end end\n"
      "next = nil\n"
      "tostring = nil\n"
      "string.format = function() return 'forged' end\n"
      "string.upper = function() return 'forged' end\n"
      "widget.ping = function() return 'forged' end\n"
      "_G.runtime = { send = function() end }\n"
      "rawset(_G, 'widget', nil)\n"
      "local ok = pcall(function() getmetatable('').__index.format = nil end)\n"
      "meta_refused = not ok\n");
  TEST_ASSERT_TRUE(flag("api_seen"));
  TEST_ASSERT_TRUE(flag("meta_refused"));

  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "probe";
  evt["data"]["a"] = 1;
  evt["data"]["b"] = 2;
  sandbox->injectMessage("test", "probe", evt);
  pump();

  const std::string* seen = nullptr;
  for (auto& e : *events)
    if (e.find("\"type\":\"probe_seen\"") != std::string::npos) seen = &e;
  TEST_ASSERT_NOT_NULL(seen);
  TEST_ASSERT_TRUE(seen->find("\"n\":2") != std::string::npos);
  TEST_ASSERT_TRUE(seen->find("\"f\":\"2\"") != std::string::npos);
  TEST_ASSERT_TRUE(seen->find("\"g\":\"2\"") != std::string::npos);
  TEST_ASSERT_TRUE(seen->find("\"up\":\"OK\"") != std::string::npos);
  TEST_ASSERT_TRUE(seen->find("\"w\":\"pong\"") != std::string::npos);
  TEST_ASSERT_TRUE(seen->find("\"t\":\"function\"") != std::string::npos);
  TEST_ASSERT_TRUE(seen->find("forged") == std::string::npos);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_replaced_base_function_does_not_reach_the_next_app);
  RUN_TEST(test_replaced_library_function_does_not_reach_the_next_app);
  RUN_TEST(test_replaced_module_function_does_not_reach_the_next_app);
  RUN_TEST(test_an_app_sees_its_own_changes);
  RUN_TEST(test_shared_metatables_are_locked);
  RUN_TEST(test_datetime_is_fresh_for_each_app);
  RUN_TEST(test_chunk_runs_in_the_app_environment);
  RUN_TEST(test_flag_off_keeps_the_previous_environment);
  RUN_TEST(test_set_app_global_reaches_the_running_app_only);
  RUN_TEST(test_framework_reads_pristine_globals);
  return UNITY_END();
}
