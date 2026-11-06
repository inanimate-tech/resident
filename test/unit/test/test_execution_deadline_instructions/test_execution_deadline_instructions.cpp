// The execution deadline's instruction backend (RESIDENT_DEADLINE_INSTRUCTIONS):
// a dispatch may run executionDeadlineMs * deadlineInstrPerMs Lua
// instructions, on whichever threads it runs them, and then ends with the
// same error the wall-clock deadline raises. The soft deadline reads the
// same count as its clock.
//
// As in test_execution_deadline, the runaway loops are finite where they can
// be, so a regression fails on the elapsed time rather than hanging.
#define RESIDENT_DEADLINE_INSTRUCTIONS 1

#include <unity.h>
#include <chrono>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

constexpr uint32_t kDeadlineMs = 50;
constexpr uint32_t kInstrPerMs = 5000;
// Wall-clock ceiling for a dispatch the guard stopped. Its budget, 250,000
// instructions, is about a millisecond on a host; the loops below would run
// for seconds.
constexpr long kLimitMs = 750;

// Some 100 million iterations: seconds on any host.
#define SPIN "for i = 1, 100000000 do end\n"

// `probe.unhook(co)`: takes a coroutine's hook away, as if it had been made
// where no dispatch was running, so it has no hook to inherit.
int luaUnhook(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTHREAD);
  lua_sethook(lua_tothread(L, 1), nullptr, 0, 0);
  return 0;
}

class ProbeExtension : public Resident::Extension {
public:
  const char* name() const override { return "probe"; }
  void registerModule(Resident::LuaModule& m) override {
    m.staticMethod("unhook", luaUnhook);
  }
};

ProbeExtension probeExtension;
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;

void build(uint32_t instrPerMs = kInstrPerMs, uint32_t softMs = 0) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.executionDeadlineMs = kDeadlineMs;
  cfg.executionSoftDeadlineMs = softMs;
  cfg.deadlineInstrPerMs = instrPerMs;
  cfg.extensions = {&probeExtension};
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

// The tick was stopped by the guard, with the wall-clock backend's error,
// and its loop never finished.
void assertStoppedAtDeadline(long elapsedMs) {
  TEST_ASSERT_LESS_THAN_INT32(kLimitMs, elapsedMs);
  TEST_ASSERT_TRUE(telemetryMentions("execution deadline exceeded (50 ms)"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("runtime_error"));
  TEST_ASSERT_FALSE(sandbox->luaGlobalBoolForTest("finished"));
}

// `while true do n = n + 1 end` is 4 instructions an iteration (GETTABUP,
// ADDI, SETTABUP, JMP), so a stopped loop's n says how many instructions the
// dispatch was given.
int iterationsFor(uint32_t instrPerMs) {
  return (int)((uint64_t)kDeadlineMs * instrPerMs / 4);
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

// An infinite loop in on_tick ends with the deadline error, once the
// dispatch has run its budget and not before.
void test_infinite_loop_in_on_tick_is_stopped(void) {
  build();
  loadApp(
      "n = 0\n"
      "function on_tick(ctx, dt) while true do n = n + 1 end end\n");
  assertStoppedAtDeadline(timedPump());
  const int expected = iterationsFor(kInstrPerMs);
  TEST_ASSERT_INT_WITHIN(expected / 100, expected,
                         sandbox->luaGlobalIntForTest("n"));
}

// The budget is executionDeadlineMs * deadlineInstrPerMs: twice the rate,
// twice the instructions.
void test_budget_follows_the_rate(void) {
  build(/*instrPerMs=*/2 * kInstrPerMs);
  loadApp(
      "n = 0\n"
      "function on_tick(ctx, dt) while true do n = n + 1 end end\n");
  assertStoppedAtDeadline(timedPump());
  const int expected = iterationsFor(2 * kInstrPerMs);
  TEST_ASSERT_INT_WITHIN(expected / 100, expected,
                         sandbox->luaGlobalIntForTest("n"));
}

// The same inside a coroutine made at load, before any dispatch was armed.
void test_loop_in_a_coroutine_is_stopped(void) {
  build();
  loadApp(
      "co = coroutine.create(function() " SPIN " finished = true end)\n"
      "function on_tick(ctx, dt) coroutine.resume(co) end\n");
  assertStoppedAtDeadline(timedPump());
}

// A coroutine with no hook of its own is hooked when it is resumed.
void test_unhooked_coroutine_is_hooked_on_resume(void) {
  build();
  loadApp(
      "co = coroutine.create(function() " SPIN " finished = true end)\n"
      "probe.unhook(co)\n"
      "function on_tick(ctx, dt) coroutine.resume(co) end\n");
  assertStoppedAtDeadline(timedPump());
}

// A coroutine made inside the dispatch, through coroutine.wrap, with the
// abort swallowed by pcall.
void test_loop_in_a_wrapped_coroutine_is_stopped(void) {
  build();
  loadApp(
      "function on_tick(ctx, dt)\n"
      "  local f = coroutine.wrap(function() " SPIN " end)\n"
      "  for n = 1, 3 do pcall(f) end\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// Neither thread runs a whole count step between switches. Re-setting a
// thread's hook at each switch would restart its count, and nothing would
// ever be counted.
void test_coroutine_ping_pong_is_counted(void) {
  build();
  loadApp(
      "local co = coroutine.wrap(function() while true do coroutine.yield() end end)\n"
      "function on_tick(ctx, dt)\n"
      "  for i = 1, 10000000 do co() end\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// Each coroutine runs a handful of instructions and is never resumed again.
void test_fresh_coroutines_are_counted(void) {
  build();
  loadApp(
      "local function f() return 1 end\n"
      "function on_tick(ctx, dt)\n"
      "  for i = 1, 10000000 do coroutine.resume(coroutine.create(f)) end\n"
      "  finished = true\n"
      "end\n");
  assertStoppedAtDeadline(timedPump());
}

// pcall catches the first abort; the guard keeps raising.
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

// An app that keeps to its budget runs every tick in full, coroutines and
// all; so do the ticks after an abort.
void test_normal_app_is_unaffected(void) {
  build();
  loadApp(
      "ticks = 0\n"
      "function on_tick(ctx, dt)\n"
      "  ticks = ticks + 1\n"
      "  if ticks == 2 then while true do end end\n"
      "  local gen = coroutine.wrap(function()\n"
      "    for i = 1, 5000 do coroutine.yield(i) end\n"
      "  end)\n"
      "  local a = 0\n"
      "  for i = 1, 5000 do a = a + gen() end\n"
      "  sum = a\n"
      "end\n");
  timedPump();
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("runtime_error"));
  TEST_ASSERT_EQUAL_INT(12502500, sandbox->luaGlobalIntForTest("sum"));
  timedPump();   // the runaway tick
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("runtime_error"));
  for (int i = 0; i < 3; i++) timedPump();
  TEST_ASSERT_EQUAL_INT(5, sandbox->luaGlobalIntForTest("ticks"));
  TEST_ASSERT_EQUAL_INT(12502500, sandbox->luaGlobalIntForTest("sum"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("runtime_error"));
}

// The soft deadline's clock is instructions / deadlineInstrPerMs: a tick of
// 150,000 instructions took 30 ms, and one of 50,000 took 10.
void test_soft_deadline_reads_instructions(void) {
  build(kInstrPerMs, /*softMs=*/20);
  loadApp(
      "ticks = 0\n"
      "function on_tick(ctx, dt)\n"
      "  ticks = ticks + 1\n"
      "  local n = ticks == 1 and 50000 or 150000\n"
      "  for i = 1, n do end\n"
      "end\n");
  testSerialLog().clear();
  timedPump();
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("slow_dispatch"));
  timedPump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("ticks"));
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("slow_dispatch"));
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("runtime_error"));
  TEST_ASSERT_TRUE(testSerialLog().find(
                       "[WARN] dispatch took 30.0 ms (soft 20 ms)\n") !=
                   std::string::npos);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_infinite_loop_in_on_tick_is_stopped);
  RUN_TEST(test_budget_follows_the_rate);
  RUN_TEST(test_loop_in_a_coroutine_is_stopped);
  RUN_TEST(test_unhooked_coroutine_is_hooked_on_resume);
  RUN_TEST(test_loop_in_a_wrapped_coroutine_is_stopped);
  RUN_TEST(test_coroutine_ping_pong_is_counted);
  RUN_TEST(test_fresh_coroutines_are_counted);
  RUN_TEST(test_pcall_loop_is_stopped);
  RUN_TEST(test_normal_app_is_unaffected);
  RUN_TEST(test_soft_deadline_reads_instructions);
  return UNITY_END();
}
