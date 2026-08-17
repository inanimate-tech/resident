// loadChunk: the update lattice's middle rung — run a Lua chunk in the
// RUNNING app's lua_State. Running state survives, re-registrations take
// effect, failures leave the app running, chunks are never persisted, and
// the deferAppLoads window DROPS chunks (never stashes them).
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

class FakeStore : public Resident::PersistentStore {
public:
  bool hasValue = false;
  String value;
  int saveCalls = 0;
  int clearCalls = 0;

  bool begin() override { return true; }
  bool save(const char* source, size_t len) override {
    saveCalls++;
    value = String(std::string(source, len));
    hasValue = true;
    return true;
  }
  String load() override { return hasValue ? value : String(); }
  void clear() override { clearCalls++; hasValue = false; value = String(); }
};

FakeStore* store = nullptr;
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;

// counter advances by greet() every tick; greet is the swappable
// registration; base and inited prove running state survives a chunk.
constexpr const char* SWAPPABLE_APP =
    "counter = 0\n"
    "base = 100\n"
    "function greet() return 1 end\n"
    "function init(ctx) inited = (inited or 0) + 1 end\n"
    "function on_tick(ctx, dt) counter = counter + greet() end\n";

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = true;
  cfg.persistentStore = store;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setTelemetryCallback([](const char* json) {
    telemetry->push_back(json ? json : "");
  });
  sandbox->setup();
}

bool telemetryHas(const char* name) {
  for (auto& t : *telemetry)
    if (t.find(std::string("\"name\":\"") + name + "\"") != std::string::npos) return true;
  return false;
}

void loadApp(const char* code) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  sandbox->injectMessage("test", "app", doc);
}

void injectChunk(const char* code) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "chunk";
  doc["code"] = code;
  sandbox->injectMessage("test", "chunk", doc);
}

void pump() { testMillis() += 200; sandbox->loop(); }  // one on_tick
}  // namespace

void setUp(void) {
  testMillis() = 0;
  store = new FakeStore();
  telemetry = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete store; store = nullptr;
  delete telemetry; telemetry = nullptr;
}

void test_chunk_swaps_registration_and_preserves_running_state(void) {
  build();
  loadApp(SWAPPABLE_APP);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("counter"));

  TEST_ASSERT_TRUE(sandbox->loadChunk("function greet() return 10 end"));
  pump();
  // New registration in effect on the next tick...
  TEST_ASSERT_EQUAL_INT(11, sandbox->luaGlobalIntForTest("counter"));
  // ...and the running state survived: counter accumulated (not reset),
  // untouched globals intact, init() NOT re-called.
  TEST_ASSERT_EQUAL_INT(100, sandbox->luaGlobalIntForTest("base"));
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("inited"));
  TEST_ASSERT_TRUE(telemetryHas("chunk_applied"));
}

void test_chunk_via_system_channel_message_and_no_persist(void) {
  build();
  loadApp(SWAPPABLE_APP);
  TEST_ASSERT_EQUAL_INT(1, store->saveCalls);          // app load persisted
  injectChunk("function greet() return 5 end");
  pump();
  TEST_ASSERT_EQUAL_INT(5, sandbox->luaGlobalIntForTest("counter"));
  // A chunk is ephemeral: NVS still holds the base generation, untouched.
  TEST_ASSERT_EQUAL_INT(1, store->saveCalls);
  TEST_ASSERT_EQUAL_INT(0, store->clearCalls);
  TEST_ASSERT_EQUAL_STRING(SWAPPABLE_APP, store->value.c_str());
}

void test_chunk_syntax_error_leaves_app_running(void) {
  build();
  loadApp(SWAPPABLE_APP);
  pump();
  TEST_ASSERT_FALSE(sandbox->loadChunk("function broken("));
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(telemetryHas("chunk_error"));
  TEST_ASSERT_FALSE(telemetryHas("chunk_applied"));
  pump();                                              // still ticking, old greet
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("counter"));
}

void test_chunk_runtime_error_leaves_app_running(void) {
  build();
  loadApp(SWAPPABLE_APP);
  pump();
  TEST_ASSERT_FALSE(sandbox->loadChunk("greet = nil; error('boom')"));
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(telemetryHas("chunk_error"));
  // NOTE: a chunk is not a transaction — statements before the error DID
  // run (greet is now nil, so the next tick errors at runtime and is caught
  // by the on_tick pcall; the app object itself stays loaded and running).
  pump();
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_chunk_redefining_on_tick_takes_effect(void) {
  build();
  loadApp(SWAPPABLE_APP);
  pump();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "function on_tick(ctx, dt) counter = counter + 1000 end"));
  pump();
  TEST_ASSERT_EQUAL_INT(1001, sandbox->luaGlobalIntForTest("counter"));
}

void test_chunk_dropped_during_defer_window(void) {
  build();
  loadApp(SWAPPABLE_APP);
  sandbox->deferAppLoads(true);
  TEST_ASSERT_FALSE(sandbox->loadChunk("function greet() return 10 end"));
  injectChunk("function greet() return 10 end");       // wire path drops too
  TEST_ASSERT_FALSE(sandbox->hasDeferredAppLoad());    // dropped, NOT stashed
  sandbox->deferAppLoads(false);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("counter"));  // old greet
}

void test_chunk_without_app_dropped(void) {
  build();
  TEST_ASSERT_FALSE(sandbox->loadChunk("x = 1"));
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_FALSE(telemetryHas("chunk_error"));      // a drop, not an error
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_chunk_swaps_registration_and_preserves_running_state);
  RUN_TEST(test_chunk_via_system_channel_message_and_no_persist);
  RUN_TEST(test_chunk_syntax_error_leaves_app_running);
  RUN_TEST(test_chunk_runtime_error_leaves_app_running);
  RUN_TEST(test_chunk_redefining_on_tick_takes_effect);
  RUN_TEST(test_chunk_dropped_during_defer_window);
  RUN_TEST(test_chunk_without_app_dropped);
  return UNITY_END();
}
