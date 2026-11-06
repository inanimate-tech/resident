// The execution guard's hard deadline, against the ways Lua can keep running
// after it: a pcall that swallows the abort, a coroutine the timer did not
// hook, and Lua run while the abort unwinds (__close and xpcall handlers).
//
// Every test is a limit check. Each app's loops are finite, a few seconds of
// work in all; the guard must end them within a fraction of that, with the
// deadline error. A regression fails on the elapsed time instead of hanging.
#include <unity.h>
#include <chrono>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

constexpr uint32_t kDeadlineMs = 50;
// Wall-clock ceiling for a dispatch the guard stopped: the deadline plus
// generous slack for a loaded CI host. The loops below would run for seconds.
constexpr long kLimitMs = 750;

// A finite loop that runs far past the deadline on any host: some 100
// million iterations, a second or more.
#define SPIN "for i = 1, 100000000 do end\n"

// `clock.advance(ms)`: moves the faked millis() the soft deadline reads, so
// a dispatch can take longer than it without spending real time.
int luaAdvanceClock(lua_State* L) {
  testMillis() += (unsigned long)luaL_checkinteger(L, 1);
  return 0;
}

class ClockExtension : public Resident::Extension {
public:
  const char* name() const override { return "clock"; }
  void registerModule(Resident::LuaModule& m) override {
    m.staticMethod("advance", luaAdvanceClock);
  }
};

ClockExtension clockExtension;
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;

void build(uint32_t softMs = 0) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.executionDeadlineMs = kDeadlineMs;
  cfg.steadyDeadlineMs = kDeadlineMs;
  cfg.executionSoftDeadlineMs = softMs;
  cfg.extensions = {&clockExtension};
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

int telemetryCount(const char* name) {
  int n = 0;
  for (auto& t : *telemetry)
    if (t.find(std::string("\"name\":\"") + name + "\"") != std::string::npos) n++;
  return n;
}

bool telemetryMentions(const char* needle) {
  for (auto& t : *telemetry)
    if (t.find(needle) != std::string::npos) return true;
  return false;
}

// One loop pass that is due a tick; returns its wall-clock milliseconds.
long timedPump() {
  testMillis() += 200;
  const auto t0 = std::chrono::steady_clock::now();
  sandbox->loop();
  return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0)
      .count();
}

// The tick that runs `body` was stopped at the deadline: quickly, and with
// the deadline error, never by running the loop out.
void assertStoppedAtDeadline(long elapsedMs) {
  TEST_ASSERT_LESS_THAN_INT32(kLimitMs, elapsedMs);
  TEST_ASSERT_TRUE(telemetryMentions("execution deadline exceeded"));
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("finished"));
}

}  // namespace

void setUp(void) {
  testMillis() = 0;
  telemetry = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  delete telemetry;
  telemetry = nullptr;
}

// pcall catches the first abort; the guard must keep raising.
void test_pcall_loop_is_stopped(void) {
  build();
  loadApp(
      "local function spin() " SPIN "end\n"
      "function on_tick(ctx, dt)\n"
      "  for n = 1, 3 do pcall(spin) end\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// The same with pcall nested, and the error swallowed at every level.
void test_nested_pcall_loop_is_stopped(void) {
  build();
  loadApp(
      "local function spin() " SPIN "end\n"
      "local function inner() for n = 1, 2 do pcall(spin) end end\n"
      "function on_tick(ctx, dt)\n"
      "  for n = 1, 2 do pcall(inner) end\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// A coroutine made before the dispatch was armed (at load) inherits no hook.
void test_coroutine_created_before_arming_is_stopped(void) {
  build();
  loadApp(
      "co = coroutine.create(function() " SPIN " finished = true end)\n"
      "function on_tick(ctx, dt) coroutine.resume(co) end\n");
  assertStoppedAtDeadline(timedPump());
}

// A coroutine made inside the dispatch, before the deadline, inherits none
// either; coroutine.wrap is the other way in.
void test_coroutine_created_after_arming_is_stopped(void) {
  build();
  loadApp(
      "function on_tick(ctx, dt)\n"
      "  local f = coroutine.wrap(function() " SPIN " end)\n"
      "  pcall(f)\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// A coroutine resumed after the deadline is hooked before it runs: the outer
// loop swallows each abort and keeps making fresh coroutines.
void test_coroutine_resumed_after_the_deadline_is_stopped(void) {
  build();
  loadApp(
      "function on_tick(ctx, dt)\n"
      "  for n = 1, 3 do\n"
      "    coroutine.resume(coroutine.create(function() " SPIN " end))\n"
      "  end\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// Two coroutines deep: the running thread is the inner one.
void test_nested_coroutines_are_stopped(void) {
  build();
  loadApp(
      "function on_tick(ctx, dt)\n"
      "  local inner = coroutine.create(function() " SPIN " end)\n"
      "  local outer = coroutine.create(function()\n"
      "    coroutine.resume(inner)\n"
      "    " SPIN
      "  end)\n"
      "  coroutine.resume(outer)\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// coroutine.close runs a suspended coroutine's __close handlers on it.
void test_coroutine_close_handlers_are_stopped(void) {
  build();
  loadApp(
      "function on_tick(ctx, dt)\n"
      "  local co = coroutine.create(function()\n"
      "    local guard <close> = setmetatable({}, {__close = function()\n"
      "      " SPIN
      "    end})\n"
      "    coroutine.yield()\n"
      "  end)\n"
      "  coroutine.resume(co)\n"
      "  pcall(function() " SPIN " end)\n"
      "  coroutine.close(co)\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// The abort unwinds through a to-be-closed variable whose handler loops.
void test_close_handler_during_unwind_is_stopped(void) {
  build();
  loadApp(
      "function on_tick(ctx, dt)\n"
      "  pcall(function()\n"
      "    local guard <close> = setmetatable({}, {__close = function()\n"
      "      " SPIN
      "      finished = true\n"
      "    end})\n"
      "    " SPIN
      "  end)\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// An xpcall message handler that loops runs as the abort is raised.
void test_xpcall_handler_is_stopped(void) {
  build();
  loadApp(
      "local function slow(e) " SPIN " return e end\n"
      "function on_tick(ctx, dt)\n"
      "  for n = 1, 3 do\n"
      "    xpcall(function() " SPIN " end, slow)\n"
      "  end\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// The coroutine and xpcall wrappers keep their stdlib behaviour.
void test_wrappers_keep_stdlib_behaviour(void) {
  build();
  loadApp(
      "local co = coroutine.create(function(a, b)\n"
      "  local c = coroutine.yield(a + b)\n"
      "  return c * 2\n"
      "end)\n"
      "local ok1, v1 = coroutine.resume(co, 1, 2)\n"
      "local ok2, v2 = coroutine.resume(co, 5)\n"
      "local ok3, e3 = coroutine.resume(co)\n"
      "resume_ok = ok1 and v1 == 3 and ok2 and v2 == 10 and not ok3\n"
      "  and e3 == 'cannot resume dead coroutine'\n"
      "local gen = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end)\n"
      "wrap_ok = gen() == 1 and gen() == 2 and gen() == 3\n"
      "local bad = coroutine.wrap(function() error('boom') end)\n"
      "local okw, ew = pcall(bad)\n"
      "wrap_err_ok = not okw and type(ew) == 'string' and ew:find('boom') ~= nil\n"
      "local closed = false\n"
      "local cc = coroutine.create(function()\n"
      "  local g <close> = setmetatable({}, {__close = function() closed = true end})\n"
      "  coroutine.yield()\n"
      "end)\n"
      "coroutine.resume(cc)\n"
      "close_ok = coroutine.close(cc) and closed and coroutine.status(cc) == 'dead'\n"
      "local okx, ex = xpcall(function() error('x', 0) end, function(m) return 'handled ' .. m end)\n"
      "xpcall_ok = not okx and ex == 'handled x'\n"
      "local okp, a, b = xpcall(function(p, q) return p, q end, print, 7, 8)\n"
      "xpcall_ret_ok = okp and a == 7 and b == 8\n"
      "local yc = coroutine.wrap(function()\n"
      "  return xpcall(function() coroutine.yield('y') return 'done' end, print)\n"
      "end)\n"
      "local y1 = yc()\n"
      "local okd, d = yc()\n"
      "xpcall_yield_ok = y1 == 'y' and okd and d == 'done'\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("resume_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("wrap_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("wrap_err_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("close_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("xpcall_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("xpcall_ret_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("xpcall_yield_ok"));
}

// After an abort the next dispatches run unhooked and in full, including one
// that resumes coroutines.
void test_next_dispatch_runs_in_full(void) {
  build();
  loadApp(
      "ticks = 0\n"
      "function on_tick(ctx, dt)\n"
      "  ticks = ticks + 1\n"
      "  if ticks == 1 then for n = 1, 3 do pcall(function() " SPIN " end) end end\n"
      "  local co = coroutine.wrap(function() local a = 0 for i = 1, 20000 do a = a + i end return a end)\n"
      "  sum = co()\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
  telemetry->clear();
  timedPump();
  timedPump();
  TEST_ASSERT_EQUAL_INT(3, sandbox->luaGlobalIntForTest("ticks"));
  TEST_ASSERT_EQUAL_INT(200010000, sandbox->luaGlobalIntForTest("sum"));
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("runtime_error"));
}

// Three hard aborts inside 30 s suspend the app, once, with app_throttled.
void test_three_hard_aborts_in_30s_throttle_the_app(void) {
  build();
  loadApp(
      "ticks = 0\n"
      "function on_tick(ctx, dt) ticks = ticks + 1 while true do end end\n");
  timedPump();
  timedPump();
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("app_throttled"));
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
  timedPump();
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("app_throttled"));
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());
  timedPump();   // suspended: no fourth dispatch
  TEST_ASSERT_EQUAL_INT(3, sandbox->luaGlobalIntForTest("ticks"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("app_throttled"));
  TEST_ASSERT_TRUE(testSerialLog().find(
                       "[WARN] 3 hard aborts in 30 s; app suspended\n") !=
                   std::string::npos);
}

// The soft deadline reports on the line log.warn writes to, and as
// slow_dispatch telemetry, and lets the dispatch finish.
void test_soft_deadline_reports_on_the_warn_line(void) {
  build(/*softMs=*/20);
  loadApp("function on_tick(ctx, dt) clock.advance(30) finished = true end\n");
  testSerialLog().clear();
  timedPump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("finished"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("slow_dispatch"));
  TEST_ASSERT_TRUE(testSerialLog().find(
                       "[WARN] dispatch took 30.0 ms (soft 20 ms)\n") !=
                   std::string::npos);
}

// Aborts spread wider than the window do not throttle.
void test_aborts_spread_over_30s_do_not_throttle(void) {
  build();
  loadApp("function on_tick(ctx, dt) while true do end end\n");
  for (int i = 0; i < 4; i++) {
    timedPump();
    testMillis() += 15000;
  }
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("app_throttled"));
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
}

// A new app starts with a clean record.
void test_new_app_clears_the_abort_record(void) {
  build();
  loadApp("function on_tick(ctx, dt) while true do end end\n");
  timedPump();
  timedPump();
  loadApp("function on_tick(ctx, dt) while true do end end\n");
  timedPump();
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("app_throttled"));
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pcall_loop_is_stopped);
  RUN_TEST(test_nested_pcall_loop_is_stopped);
  RUN_TEST(test_coroutine_created_before_arming_is_stopped);
  RUN_TEST(test_coroutine_created_after_arming_is_stopped);
  RUN_TEST(test_coroutine_resumed_after_the_deadline_is_stopped);
  RUN_TEST(test_nested_coroutines_are_stopped);
  RUN_TEST(test_coroutine_close_handlers_are_stopped);
  RUN_TEST(test_close_handler_during_unwind_is_stopped);
  RUN_TEST(test_xpcall_handler_is_stopped);
  RUN_TEST(test_wrappers_keep_stdlib_behaviour);
  RUN_TEST(test_next_dispatch_runs_in_full);
  RUN_TEST(test_three_hard_aborts_in_30s_throttle_the_app);
  RUN_TEST(test_soft_deadline_reports_on_the_warn_line);
  RUN_TEST(test_aborts_spread_over_30s_do_not_throttle);
  RUN_TEST(test_new_app_clears_the_abort_record);
  return UNITY_END();
}
