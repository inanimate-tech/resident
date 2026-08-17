// Lua `store` module (arc A4): app-scoped persistent KV slot. Survives
// loadApp (same namespace) and reboot; a different storeNs clears it;
// write-through to the PersistentStore is debounced plus flushed on app
// unload; 2KB budget with no partial writes.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

class FakeStore : public Resident::PersistentStore {
public:
  // app-source slot (unused by most tests here)
  bool save(const char* source, size_t len) override {
    appValue = String(std::string(source, len));
    return true;
  }
  String load() override { return appValue; }
  void clear() override { appValue = String(); }

  // store slot
  int saveStoreCalls = 0;
  String storeBlob;
  bool saveStore(const char* json, size_t len) override {
    saveStoreCalls++;
    storeBlob = String(std::string(json, len));
    return true;
  }
  String loadStore() override { return storeBlob; }
  void clearStore() override { storeBlob = String(); }

  String appValue;
};

FakeStore* store = nullptr;
Resident::Sandbox* sandbox = nullptr;

constexpr const char* IDLE_APP =
    "function init(ctx) end\n"
    "function on_tick(ctx, dt) end\n";

// init() reads the slot into globals so tests can observe what a freshly
// loaded app sees.
constexpr const char* READER_APP =
    "function init(ctx)\n"
    "  got = store.get('count')\n"
    "  got_is_nil = (got == nil)\n"
    "end\n"
    "function on_tick(ctx, dt) end\n";

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = true;
  cfg.persistentStore = store;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}

void loadAppNs(const char* code, const char* ns) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  if (ns) doc["storeNs"] = ns;
  sandbox->injectMessage("test", "app", doc);
}

void pump(int n = 1) {
  for (int i = 0; i < n; i++) { testMillis() += 200; sandbox->loop(); }
}
}  // namespace

void setUp(void) {
  testMillis() = 0;
  store = new FakeStore();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete store; store = nullptr;
}

void test_store_scalar_round_trips(void) {
  build();
  loadAppNs(IDLE_APP, "t1");
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "ok = store.set('s', 'he said \"hi\"') and store.set('i', 7)\n"
      "     and store.set('f', 1.5) and store.set('bt', true)\n"
      "     and store.set('bf', false)\n"
      "round = store.get('s') == 'he said \"hi\"' and store.get('i') == 7\n"
      "        and math.type(store.get('i')) == 'integer'\n"
      "        and store.get('f') == 1.5 and store.get('bt') == true\n"
      "        and store.get('bf') == false\n"
      "        and store.get('missing') == nil\n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("round"));
  // Non-scalars are rejected; nil deletes.
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "rejected = (store.set('t', {}) == false)\n"
      "store.set('i', nil)\n"
      "deleted = (store.get('i') == nil)\n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("rejected"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("deleted"));
}

void test_store_survives_loadapp_same_ns(void) {
  build();
  loadAppNs(IDLE_APP, "critter");
  TEST_ASSERT_TRUE(sandbox->loadChunk("store.set('count', 7)"));
  loadAppNs(READER_APP, "critter");            // same identity → survives
  TEST_ASSERT_EQUAL_INT(7, sandbox->luaGlobalIntForTest("got"));
}

void test_store_cleared_on_different_ns(void) {
  build();
  loadAppNs(IDLE_APP, "critter");
  TEST_ASSERT_TRUE(sandbox->loadChunk("store.set('count', 7)"));
  loadAppNs(READER_APP, "otherapp");           // new identity → fresh slot
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("got_is_nil"));
}

void test_store_missing_ns_is_shared_default(void) {
  build();
  loadAppNs(IDLE_APP, nullptr);                // default ns "app"
  TEST_ASSERT_TRUE(sandbox->loadChunk("store.set('count', 3)"));
  loadAppNs(READER_APP, nullptr);              // same default → survives
  TEST_ASSERT_EQUAL_INT(3, sandbox->luaGlobalIntForTest("got"));
  loadAppNs(READER_APP, "named");              // leaving the default clears
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("got_is_nil"));
}

void test_store_survives_reboot(void) {
  build();
  loadAppNs(IDLE_APP, "critter");
  TEST_ASSERT_TRUE(sandbox->loadChunk("store.set('count', 42)"));
  pump(12);                                    // > 2s quiet → flushed to NVS
  TEST_ASSERT_NOT_NULL(strstr(store->storeBlob.c_str(), "\"ns\":\"critter\""));
  TEST_ASSERT_NOT_NULL(strstr(store->storeBlob.c_str(), "\"count\":42"));

  delete sandbox;                              // reboot: same NVS, new sandbox
  sandbox = nullptr;
  build();
  loadAppNs(READER_APP, "critter");            // same identity after reboot
  TEST_ASSERT_EQUAL_INT(42, sandbox->luaGlobalIntForTest("got"));

  delete sandbox;                              // reboot again, new identity
  sandbox = nullptr;
  build();
  loadAppNs(READER_APP, "stranger");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("got_is_nil"));
}

void test_store_debounced_write_through(void) {
  build();
  loadAppNs(IDLE_APP, "t2");
  int base = store->saveStoreCalls;            // ns switch flushed once
  TEST_ASSERT_TRUE(sandbox->loadChunk("store.set('a', 1)"));
  pump(5);                                     // 1s of quiet: not yet
  TEST_ASSERT_EQUAL_INT(base, store->saveStoreCalls);
  TEST_ASSERT_TRUE(sandbox->loadChunk("store.set('a', 2)"));  // resets quiet
  pump(5);                                     // 1s after last mutation
  TEST_ASSERT_EQUAL_INT(base, store->saveStoreCalls);
  pump(7);                                     // past 2s of quiet → one write
  TEST_ASSERT_EQUAL_INT(base + 1, store->saveStoreCalls);
  pump(10);                                    // no mutations → no more writes
  TEST_ASSERT_EQUAL_INT(base + 1, store->saveStoreCalls);
  TEST_ASSERT_NOT_NULL(strstr(store->storeBlob.c_str(), "\"a\":2"));
}

void test_store_flushed_on_app_unload(void) {
  build();
  loadAppNs(IDLE_APP, "t3");
  int base = store->saveStoreCalls;
  TEST_ASSERT_TRUE(sandbox->loadChunk("store.set('a', 1)"));
  TEST_ASSERT_EQUAL_INT(base, store->saveStoreCalls);   // debounce pending
  loadAppNs(IDLE_APP, "t3");                   // unload boundary → flush now
  TEST_ASSERT_EQUAL_INT(base + 1, store->saveStoreCalls);
  TEST_ASSERT_NOT_NULL(strstr(store->storeBlob.c_str(), "\"a\":1"));
}

void test_store_budget_rejected_no_partial_write(void) {
  build();
  loadAppNs(IDLE_APP, "t4");
  // A single over-budget value: rejected outright, key absent.
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "big_rejected = (store.set('big', string.rep('x', 3000)) == false)\n"
      "big_absent = (store.get('big') == nil)\n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("big_rejected"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("big_absent"));
  // Fill near the limit, then push over: the write is rejected whole and
  // the existing value is untouched (no partial state).
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "ok1 = store.set('base', string.rep('y', 1900))\n"
      "over = (store.set('more', string.rep('z', 500)) == false)\n"
      "intact = (#store.get('base') == 1900) and (store.get('more') == nil)\n"
      "overwrite_over = (store.set('base', string.rep('w', 2500)) == false)\n"
      "still_y = store.get('base'):sub(1, 1) == 'y'\n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok1"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("over"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("intact"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("overwrite_over"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("still_y"));
}

void test_store_keys_and_clear(void) {
  build();
  loadAppNs(IDLE_APP, "t5");
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "store.set('one', 1); store.set('two', 2)\n"
      "ks = store.keys()\n"
      "n = #ks\n"
      "seen = {}\n"
      "for _, k in ipairs(ks) do seen[k] = true end\n"
      "both = (seen.one and seen.two) == true\n"
      "store.clear()\n"
      "empty = (#store.keys() == 0) and (store.get('one') == nil)\n"));
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("both"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("empty"));
  pump(12);                                    // clear persists after quiet
  TEST_ASSERT_NOT_NULL(strstr(store->storeBlob.c_str(), "\"kv\":{}"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_store_scalar_round_trips);
  RUN_TEST(test_store_survives_loadapp_same_ns);
  RUN_TEST(test_store_cleared_on_different_ns);
  RUN_TEST(test_store_missing_ns_is_shared_default);
  RUN_TEST(test_store_survives_reboot);
  RUN_TEST(test_store_debounced_write_through);
  RUN_TEST(test_store_flushed_on_app_unload);
  RUN_TEST(test_store_keys_and_clear);
  RUN_TEST(test_store_budget_rejected_no_partial_write);
  return UNITY_END();
}
