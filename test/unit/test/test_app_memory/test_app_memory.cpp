// The app memory ceiling: LuaHeap's limit on bytes held, and the Sandbox
// that fixes it after boot and holds it only while a dispatch runs; and the
// Lua panic handler.
#include <unity.h>
#include <cstdlib>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

using Resident::LuaHeap;

namespace {

// ── LuaHeap over malloc ──────────────────────────────────────────────────

void* mallocAligned(size_t align, size_t size, void*) {
  void* p = nullptr;
  return posix_memalign(&p, align, size) == 0 ? p : nullptr;
}
void* mallocAlloc(size_t size, void*) { return malloc(size); }
void* mallocRealloc(void* ptr, size_t, size_t newSize, void*) { return realloc(ptr, newSize); }
void mallocRelease(void* ptr, size_t, void*) { free(ptr); }

LuaHeap::Backing backing() {
  return {mallocAligned, mallocAlloc, mallocRealloc, mallocRelease, nullptr};
}

size_t g_external = 0;
size_t externalBytes(void*) { return g_external; }

// ── Sandbox ──────────────────────────────────────────────────────────────

Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;
size_t g_probe = 0;

void build(size_t appMemoryBytes, bool probe = false) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.appMemoryBytes = appMemoryBytes;
  if (probe) cfg.appMemoryProbe = []() -> size_t { return g_probe; };
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

int telemetryWith(const char* name, const char* needle) {
  int n = 0;
  for (auto& t : *telemetry) {
    if (t.find(std::string("\"name\":\"") + name + "\"") == std::string::npos) continue;
    if (needle && t.find(needle) == std::string::npos) continue;
    n++;
  }
  return n;
}

}  // namespace

void setUp(void) {
  g_external = 0;
  g_probe = 0;
  telemetry = new std::vector<std::string>();
}

void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  delete telemetry;
  telemetry = nullptr;
}

// ── LuaHeap ──────────────────────────────────────────────────────────────

void test_no_limit_by_default(void) {
  LuaHeap heap(backing());
  TEST_ASSERT_EQUAL_UINT(0, heap.limit());
  void* p = heap.reallocate(nullptr, 0, 64 * 1024);
  TEST_ASSERT_NOT_NULL(p);
  heap.reallocate(p, 64 * 1024, 0);
}

void test_a_large_block_past_the_limit_is_refused(void) {
  LuaHeap heap(backing());
  heap.setLimit(10000);
  void* a = heap.reallocate(nullptr, 0, 6000);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NULL(heap.reallocate(nullptr, 0, 6000));
  TEST_ASSERT_EQUAL_UINT(6000, heap.held());
  void* b = heap.reallocate(nullptr, 0, 4000);   // exactly to the limit
  TEST_ASSERT_NOT_NULL(b);
  heap.reallocate(a, 6000, 0);
  heap.reallocate(b, 4000, 0);
}

void test_it_counts_slabs_held_not_live_bytes(void) {
  LuaHeap heap(backing());
  heap.setLimit(LuaHeap::kSlabBytes);
  void* a = heap.reallocate(nullptr, 0, 16);   // the one slab the limit allows
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_UINT(LuaHeap::kSlabBytes, heap.held());
  // The same class fits in the slab it has: no new memory, no refusal.
  void* b = heap.reallocate(nullptr, 0, 16);
  TEST_ASSERT_NOT_NULL(b);
  // Another class needs another slab, though only 32 bytes are live.
  TEST_ASSERT_NULL(heap.reallocate(nullptr, 0, 100));
  heap.reallocate(a, 16, 0);
  heap.reallocate(b, 16, 0);
}

void test_a_growing_realloc_past_the_limit_is_refused(void) {
  LuaHeap heap(backing());
  heap.setLimit(10000);
  void* a = heap.reallocate(nullptr, 0, 6000);
  TEST_ASSERT_NULL(heap.reallocate(a, 6000, 12000));
  TEST_ASSERT_EQUAL_UINT(6000, heap.held());   // a is still held, unchanged
  void* b = heap.reallocate(a, 6000, 9000);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_UINT(9000, heap.held());
  heap.reallocate(b, 9000, 0);
}

void test_a_shrink_is_never_refused(void) {
  LuaHeap heap(backing());
  void* big = heap.reallocate(nullptr, 0, 1000);
  heap.setLimit(1);   // far below what is held
  // Large to large, and large to a small class with no slab yet.
  void* smaller = heap.reallocate(big, 1000, 600);
  TEST_ASSERT_NOT_NULL(smaller);
  void* small = heap.reallocate(smaller, 600, 40);
  TEST_ASSERT_NOT_NULL(small);
  TEST_ASSERT_EQUAL_UINT(1, heap.limit());   // restored after the shrink
  heap.setLimit(0);
  heap.reallocate(small, 40, 0);
}

void test_external_bytes_count_toward_the_limit(void) {
  LuaHeap heap(backing());
  heap.setExternal(externalBytes, nullptr);
  heap.setLimit(10000);
  g_external = 5000;
  TEST_ASSERT_EQUAL_UINT(5000, heap.charged());
  TEST_ASSERT_NULL(heap.reallocate(nullptr, 0, 6000));
  g_external = 0;
  void* a = heap.reallocate(nullptr, 0, 6000);
  TEST_ASSERT_NOT_NULL(a);
  heap.reallocate(a, 6000, 0);
}

void test_lua_sees_the_limit_as_out_of_memory_and_carries_on(void) {
  LuaHeap heap(backing());
  lua_State* L = lua_newstate(LuaHeap::luaAlloc, &heap);
  luaL_openlibs(L);
  heap.setLimit(heap.held() + 64 * 1024);
  TEST_ASSERT_EQUAL_INT(LUA_OK, luaL_loadstring(L, "local t = {} for i = 1, 1e6 do t[i] = i end"));
  TEST_ASSERT_EQUAL_INT(LUA_ERRMEM, lua_pcall(L, 0, 0, 0));
  lua_pop(L, 1);
  lua_gc(L, LUA_GCCOLLECT, 0);
  TEST_ASSERT_EQUAL_INT(LUA_OK, luaL_dostring(L, "x = string.rep('a', 1000)"));
  heap.setLimit(0);
  lua_close(L);
}

// ── Sandbox ──────────────────────────────────────────────────────────────

void test_the_ceiling_is_the_baseline_plus_the_budget(void) {
  build(256 * 1024);
  const size_t ceiling = sandbox->appMemoryCeiling();
  TEST_ASSERT_TRUE(ceiling > 256 * 1024);
  TEST_ASSERT_TRUE(sandbox->appMemoryCharged() <= ceiling - 256 * 1024 + 4096);
}

void test_the_default_budget_is_one_megabyte_on_a_host(void) {
  build(0);
  const size_t ceiling = sandbox->appMemoryCeiling();
  TEST_ASSERT_TRUE(ceiling > 1024 * 1024);
  TEST_ASSERT_TRUE(ceiling < 2 * 1024 * 1024);
}

void test_unlimited_sets_no_ceiling(void) {
  build(Resident::SandboxConfig::kAppMemoryUnlimited);
  TEST_ASSERT_EQUAL_UINT(0, sandbox->appMemoryCeiling());
}

void test_an_app_past_its_ceiling_gets_an_error_and_the_next_app_runs(void) {
  build(128 * 1024);
  const size_t ceiling = sandbox->appMemoryCeiling();
  loadApp(
      "t = {}\n"
      "function on_tick(ctx, dt)\n"
      "  for i = 1, 10000 do t[#t + 1] = string.rep('x', 100) .. i end\n"
      "end\n");
  sandbox->callOnTickForTest(16);
  TEST_ASSERT_EQUAL_INT(1, telemetryWith("runtime_error", "not enough memory"));
  TEST_ASSERT_TRUE(sandbox->appMemoryCharged() <= ceiling);

  // Outside a dispatch nothing is refused: the load resets the environment
  // and builds the next app's ctx with no protected call around it.
  loadApp("ok = 0\nfunction on_tick(ctx, dt) ok = ok + 1 end\n");
  sandbox->callOnTickForTest(16);
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("ok"));
  // A load does not move the ceiling.
  TEST_ASSERT_EQUAL_UINT(ceiling, sandbox->appMemoryCeiling());
}

void test_the_probe_is_charged_to_the_app(void) {
  build(128 * 1024, /*probe=*/true);
  loadApp(
      "function init(ctx) big = string.rep('y', 32 * 1024) end\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_EQUAL_INT(0, telemetryWith("runtime_error", "not enough memory"));

  g_probe = 200 * 1024;   // as if the app's widgets held 200 KB
  loadApp(
      "function init(ctx) big = string.rep('y', 32 * 1024) end\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_EQUAL_INT(1, telemetryWith("runtime_error", "not enough memory"));
}

void test_a_panic_is_recorded_and_reported_at_the_next_setup(void) {
  lua_State* L = luaL_newstate();
  lua_pushstring(L, "unprotected error in call to Lua API (not enough memory)");
  Resident::Sandbox::luaPanicForTest(L);
  lua_close(L);

  build(0);
  TEST_ASSERT_NOT_NULL(Resident::Sandbox::lastLuaPanic());
  TEST_ASSERT_EQUAL_STRING("unprotected error in call to Lua API (not enough memory)",
                           Resident::Sandbox::lastLuaPanic());
  TEST_ASSERT_EQUAL_INT(1, telemetryWith("lua_panic", "not enough memory"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_limit_by_default);
  RUN_TEST(test_a_large_block_past_the_limit_is_refused);
  RUN_TEST(test_it_counts_slabs_held_not_live_bytes);
  RUN_TEST(test_a_growing_realloc_past_the_limit_is_refused);
  RUN_TEST(test_a_shrink_is_never_refused);
  RUN_TEST(test_external_bytes_count_toward_the_limit);
  RUN_TEST(test_lua_sees_the_limit_as_out_of_memory_and_carries_on);
  RUN_TEST(test_the_ceiling_is_the_baseline_plus_the_budget);
  RUN_TEST(test_the_default_budget_is_one_megabyte_on_a_host);
  RUN_TEST(test_unlimited_sets_no_ceiling);
  RUN_TEST(test_an_app_past_its_ceiling_gets_an_error_and_the_next_app_runs);
  RUN_TEST(test_the_probe_is_charged_to_the_app);
  RUN_TEST(test_a_panic_is_recorded_and_reported_at_the_next_setup);
  return UNITY_END();
}
