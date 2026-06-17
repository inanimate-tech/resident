// Behavioral tests for app persistence: save-on-success, restore, and the
// boot countdown. Compiles the real ResidentSandbox.cpp against the native
// stub set, networkless. A FakePersistentStore stands in for NVS; a
// SpyDisplay captures status text; a SpyButton drives the countdown skip.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

class FakeStore : public Resident::PersistentStore {
public:
  bool beginResult = true;
  bool saveResult = true;      // set false to simulate "too big"
  bool hasValue = false;
  String value;
  int saveCalls = 0;
  int clearCalls = 0;

  bool begin() override { return beginResult; }
  bool save(const char* source, size_t len) override {
    saveCalls++;
    if (!saveResult) return false;
    value = String(std::string(source, len));
    hasValue = true;
    return true;
  }
  String load() override { return hasValue ? value : String(); }
  void clear() override { clearCalls++; hasValue = false; value = String(); }
};

class SpyDisplay : public Resident::StatusDisplay {
public:
  std::vector<std::string> texts;
  void displayText(const char* text) override { texts.push_back(text ? text : ""); }
  std::string last() const { return texts.empty() ? "" : texts.back(); }
};

class SpyButton : public Resident::SystemButton {
public:
  bool down = false;
  bool pressed() override { return down; }
};

FakeStore* store = nullptr;
SpyDisplay* display = nullptr;
SpyButton* button = nullptr;
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;

constexpr const char* GOOD_APP =
    "function init(ctx) end\n"
    "function on_tick(ctx, dt) end\n";

// Build a sandbox with the given config knobs already applied, then setup().
void makeSandbox(bool persist, bool withButton) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.statusDisplay = display;
  cfg.persistApps = persist;
  cfg.persistentStore = store;
  if (withButton) cfg.systemButton = button;
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

}  // namespace

void setUp(void) {
  testMillis() = 0;
  store = new FakeStore();
  display = new SpyDisplay();
  button = new SpyButton();
  telemetry = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete store; store = nullptr;
  delete display; display = nullptr;
  delete button; button = nullptr;
  delete telemetry; telemetry = nullptr;
}

void test_save_after_successful_load(void) {
  makeSandbox(/*persist=*/true, /*withButton=*/false);
  sandbox->loadApp(GOOD_APP);
  TEST_ASSERT_TRUE(store->hasValue);
  TEST_ASSERT_EQUAL_STRING(GOOD_APP, store->value.c_str());
}

void test_no_save_on_compile_failure(void) {
  makeSandbox(true, false);
  sandbox->loadApp("this is not valid lua %%%");
  TEST_ASSERT_FALSE(store->hasValue);
}

void test_no_save_on_init_failure(void) {
  makeSandbox(true, false);
  sandbox->loadApp("function init(ctx) error('boom') end\n"
                   "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_FALSE(store->hasValue);   // compiled, but init() errored
}

void test_oversized_save_emits_telemetry(void) {
  store->saveResult = false;            // medium rejects the blob
  makeSandbox(true, false);
  sandbox->loadApp(GOOD_APP);
  TEST_ASSERT_FALSE(store->hasValue);
  TEST_ASSERT_TRUE(telemetryHas("persist_too_big"));
}

void test_persist_disabled_never_saves(void) {
  makeSandbox(/*persist=*/false, false);
  sandbox->loadApp(GOOD_APP);
  TEST_ASSERT_EQUAL_INT(0, store->saveCalls);
}

void test_countdown_shows_deviceid_and_counts_down(void) {
  store->save(GOOD_APP, strlen(GOOD_APP));   // seed a saved app
  makeSandbox(true, false);                  // setup() arms the countdown

  sandbox->loop();                            // t=0 -> "20s"
  TEST_ASSERT_EQUAL_STRING("native-test a4cf1200\n\n20s", display->last().c_str());
  TEST_ASSERT_FALSE(sandbox->isAppRunning()); // not loaded during countdown

  testMillis() = 5000;                        // 15s remaining
  sandbox->loop();
  TEST_ASSERT_EQUAL_STRING("native-test a4cf1200\n\n15s", display->last().c_str());
}

void test_countdown_autoloads_after_20s(void) {
  store->save(GOOD_APP, strlen(GOOD_APP));
  makeSandbox(true, false);

  sandbox->loop();                    // counting down
  TEST_ASSERT_FALSE(sandbox->isAppRunning());

  testMillis() = 20000;               // reach the deadline
  sandbox->loop();
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(telemetryHas("app_restored"));
}

void test_button_skips_countdown(void) {
  store->save(GOOD_APP, strlen(GOOD_APP));
  makeSandbox(/*persist=*/true, /*withButton=*/true);
  button->down = true;                // pressed at t=0

  sandbox->loop();
  TEST_ASSERT_TRUE(sandbox->isAppRunning());   // loaded immediately
  TEST_ASSERT_TRUE(telemetryHas("app_restored"));
}

void test_network_push_cancels_countdown(void) {
  store->save(GOOD_APP, strlen(GOOD_APP));
  makeSandbox(true, false);
  sandbox->loop();                    // counting down

  const char* OTHER = "function on_tick(ctx, dt) end\n";
  sandbox->loadApp(OTHER);            // simulates an incoming push
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_EQUAL_STRING(OTHER, store->value.c_str());  // the push was saved

  // Past the old deadline: the persisted app must NOT reload over the push.
  testMillis() = 25000;
  sandbox->loop();
  TEST_ASSERT_EQUAL_STRING(OTHER, store->value.c_str());
}

void test_restore_failure_discards_and_falls_back(void) {
  // Seed an app that compiles but errors in init() — simulates a reflashed
  // sandbox whose API the saved app no longer satisfies.
  const char* BAD = "function init(ctx) error('gone') end\n"
                    "function on_tick(ctx, dt) end\n";
  store->save(BAD, strlen(BAD));
  makeSandbox(true, false);

  testMillis() = 20000;
  sandbox->loop();                    // countdown fires the restore

  TEST_ASSERT_FALSE(sandbox->isAppRunning());          // not left running
  TEST_ASSERT_FALSE(store->hasValue);                  // discarded
  TEST_ASSERT_TRUE(store->clearCalls >= 1);
  TEST_ASSERT_TRUE(telemetryHas("persist_load_failed"));
  TEST_ASSERT_EQUAL_STRING("native-test a4cf1200", display->last().c_str());
}

void test_persist_disabled_no_countdown(void) {
  store->save(GOOD_APP, strlen(GOOD_APP));   // store has data...
  makeSandbox(/*persist=*/false, false);     // ...but persistence is off

  sandbox->loop();
  testMillis() = 25000;
  sandbox->loop();
  TEST_ASSERT_FALSE(sandbox->isAppRunning());            // no auto-load
  TEST_ASSERT_EQUAL_INT(0, (int)display->texts.size());  // no countdown text
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_save_after_successful_load);
  RUN_TEST(test_no_save_on_compile_failure);
  RUN_TEST(test_no_save_on_init_failure);
  RUN_TEST(test_oversized_save_emits_telemetry);
  RUN_TEST(test_persist_disabled_never_saves);
  RUN_TEST(test_countdown_shows_deviceid_and_counts_down);
  RUN_TEST(test_countdown_autoloads_after_20s);
  RUN_TEST(test_button_skips_countdown);
  RUN_TEST(test_network_push_cancels_countdown);
  RUN_TEST(test_restore_failure_discards_and_falls_back);
  RUN_TEST(test_persist_disabled_no_countdown);
  UNITY_END();
  return 0;
}
