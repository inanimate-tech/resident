// The app quarantine through the sandbox: crashes counted across simulated
// resets, the persisted app held back, a re-sent copy refused, the hello and
// the idle screen, and everything that clears it. Compiles the real
// ResidentSandbox.cpp, networkless. A reset is simulated by deleting the
// sandbox and building another one, with the host's stand-ins for RTC memory
// (s_appHealthRecord, which outlives the sandbox as RTC memory outlives a
// reset) and the reset reason (s_hostResetKind).
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

using Resident::app_health::ResetKind;

namespace {

class FakeStore : public Resident::PersistentStore {
public:
  bool hasValue = false;
  String value;
  bool hasQuarantine = false;
  uint32_t qHash = 0, qCrashes = 0;
  int quarantineSaves = 0;

  bool save(const char* source, size_t len) override {
    value = String(std::string(source, len));
    hasValue = true;
    return true;
  }
  String load() override { return hasValue ? value : String(); }
  void clear() override { hasValue = false; value = String(); }

  bool saveQuarantine(uint32_t hash, uint32_t crashes) override {
    quarantineSaves++;
    hasQuarantine = true; qHash = hash; qCrashes = crashes;
    return true;
  }
  bool loadQuarantine(uint32_t& hash, uint32_t& crashes) override {
    if (!hasQuarantine) return false;
    hash = qHash; crashes = qCrashes;
    return true;
  }
  void clearQuarantine() override { hasQuarantine = false; qHash = qCrashes = 0; }
};

class SpyDisplay : public Resident::SystemDisplay {
public:
  const char* name() const override { return "spy-display"; }
  std::vector<std::string> texts;
  void displayText(const char* text) override { texts.push_back(text ? text : ""); }
  std::string last() const { return texts.empty() ? "" : texts.back(); }
};

uint32_t armedSeenAtAppStart = 0;

class ArmProbe : public Resident::Extension {
public:
  const char* name() const override { return "arm-probe"; }
  void onAppRunning(bool running) override {
    if (running) armedSeenAtAppStart = s_appHealthRecord.armed;
  }
};
ArmProbe armProbe;

class SpyButton : public Resident::SystemButton {
public:
  const char* name() const override { return "spy-button"; }
  bool down = false;
  bool pressed() override { return down; }
};

FakeStore* store = nullptr;
SpyDisplay* display = nullptr;
SpyButton* button = nullptr;
Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* telemetry = nullptr;
std::vector<std::string>* frames = nullptr;

constexpr const char* APP =
    "function init(ctx) end\n"
    "function on_tick(ctx, dt) end\n";
constexpr const char* OTHER_APP = "function on_tick(ctx, dt) end\n";

struct Opts {
  bool withDisplay = true;
  bool withButton = false;
};

void boot(ResetKind lastReset, Opts o = Opts()) {
  delete sandbox;
  s_hostResetKind = lastReset;
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = true;
  cfg.persistentStore = store;
  cfg.extensions = {&armProbe};
  if (o.withDisplay) cfg.systemDisplay = display;
  if (o.withButton) cfg.systemButton = button;
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

// Run the app (by load, as a host would send it) and then crash: the next
// boot is an abnormal reset with the app still armed.
void runThenCrash(Opts o = Opts()) {
  sandbox->loadApp(APP);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  boot(ResetKind::Abnormal, o);
}

void crashLimitTimes(Opts o = Opts()) {
  boot(ResetKind::PowerOn, o);
  for (int i = 0; i < 3; i++) runThenCrash(o);
}

int telemetryCount(const char* name) {
  int n = 0;
  for (auto& t : *telemetry)
    if (t.find(std::string("\"name\":\"") + name + "\"") != std::string::npos) n++;
  return n;
}

std::string lastHello() {
  frames->clear();
  sandbox->requestHello();
  sandbox->loop();
  for (auto& f : *frames)
    if (f.find("\"type\":\"hello\"") != std::string::npos) return f;
  return "";
}

}  // namespace

void setUp(void) {
  testMillis() = 0;
  s_appHealthRecord = Resident::app_health::Record{};
  s_hostResetKind = ResetKind::PowerOn;
  armedSeenAtAppStart = 0;
  store = new FakeStore();
  display = new SpyDisplay();
  button = new SpyButton();
  telemetry = new std::vector<std::string>();
  frames = new std::vector<std::string>();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete store; store = nullptr;
  delete display; display = nullptr;
  delete button; button = nullptr;
  delete telemetry; telemetry = nullptr;
  delete frames; frames = nullptr;
}

void test_two_crashes_do_not_quarantine(void) {
  boot(ResetKind::PowerOn);
  runThenCrash();
  runThenCrash();
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  sandbox->loadApp(APP);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_third_crash_quarantines_and_persists_it(void) {
  crashLimitTimes();
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
  TEST_ASSERT_TRUE(store->hasQuarantine);
  TEST_ASSERT_EQUAL_UINT32(3, store->qCrashes);
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("app_quarantined"));
  TEST_ASSERT_TRUE(store->hasValue);   // the source is kept
}

void test_quarantine_survives_a_power_cycle(void) {
  crashLimitTimes();
  boot(ResetKind::PowerOn);            // RTC record gone; NVS remembers
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
}

void test_quarantined_app_is_not_restored(void) {
  crashLimitTimes();
  sandbox->loop();
  testMillis() = 30000;                // well past the countdown
  sandbox->loop();
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, display->last().find("App paused"));
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("app_restored"));
}

void test_resending_the_quarantined_app_is_refused(void) {
  crashLimitTimes();
  sandbox->loadApp(APP);
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
  TEST_ASSERT_EQUAL_INT(2, telemetryCount("app_quarantined"));
}

void test_a_different_app_runs_and_clears_the_quarantine(void) {
  crashLimitTimes();
  sandbox->loadApp(OTHER_APP);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  TEST_ASSERT_FALSE(store->hasQuarantine);
}

void test_hello_reports_the_quarantine(void) {
  crashLimitTimes();
  std::string hello = lastHello();
  TEST_ASSERT_NOT_EQUAL(std::string::npos, hello.find("\"quarantined\":{\"crashes\":3}"));
  sandbox->clearPersistedApp();
  hello = lastHello();
  TEST_ASSERT_EQUAL(std::string::npos, hello.find("quarantined"));
}

void test_forget_clears_the_quarantine(void) {
  crashLimitTimes();
  sandbox->clearPersistedApp();
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  TEST_ASSERT_FALSE(store->hasQuarantine);
  TEST_ASSERT_FALSE(store->hasValue);
  TEST_ASSERT_EQUAL(std::string::npos, display->last().find("App paused"));
  sandbox->loadApp(APP);               // accepted again
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
}

void test_deliberate_restarts_do_not_count(void) {
  boot(ResetKind::PowerOn);
  for (int i = 0; i < 5; i++) {
    sandbox->loadApp(APP);
    boot(ResetKind::Deliberate);
  }
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
}

void test_shutdown_handler_disarms(void) {
  boot(ResetKind::PowerOn);
  sandbox->loadApp(APP);
  runThenCrash();
  runThenCrash();
  sandbox->loadApp(APP);
  disarmAppHealthOnShutdown();
  boot(ResetKind::Abnormal);           // even read as a crash, nothing was armed
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
}

void test_clean_running_clears_the_count(void) {
  boot(ResetKind::PowerOn);
  runThenCrash();
  runThenCrash();
  sandbox->loadApp(APP);
  testMillis() += 10UL * 60UL * 1000UL;   // ten clean minutes
  sandbox->loop();
  boot(ResetKind::Abnormal);           // count was cleared: this is crash 1
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  runThenCrash();                      // 2
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  runThenCrash();                      // 3
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
}

// The record is armed before init() runs, so a crash inside init() counts:
// onAppRunning(true) fires just before init(), and sees it armed.
void test_armed_before_init_runs(void) {
  boot(ResetKind::PowerOn);
  sandbox->loadApp(APP);
  TEST_ASSERT_EQUAL_UINT32(1, armedSeenAtAppStart);
  boot(ResetKind::Abnormal);
  TEST_ASSERT_EQUAL_UINT32(1, s_appHealthRecord.count);
}

void test_failed_load_disarms(void) {
  boot(ResetKind::PowerOn);
  sandbox->loadApp("this is not lua %%%");
  TEST_ASSERT_EQUAL_UINT32(0, s_appHealthRecord.armed);
}

constexpr const char* PAUSED_SCREEN =
    "App paused\nDevice ID: a4cf1200\nType: native-test";

// A quarantined app gets no boot countdown, even where a display and a
// button could run one: the device rests on "App paused" at once.
void test_quarantined_app_gets_no_countdown(void) {
  Opts o; o.withButton = true;
  boot(ResetKind::PowerOn, o);
  runThenCrash(o);
  runThenCrash(o);
  sandbox->loadApp(APP);
  display->texts.clear();              // only the tripping boot's frames
  boot(ResetKind::Abnormal, o);        // crash 3: quarantined
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
  TEST_ASSERT_EQUAL_STRING(PAUSED_SCREEN, display->last().c_str());
  for (int i = 0; i < 25; i++) {       // through where a countdown would run
    sandbox->loop();
    testMillis() += 1000;
  }
  sandbox->loop();
  for (auto& t : display->texts) {
    TEST_ASSERT_EQUAL_STRING(PAUSED_SCREEN, t.c_str());   // no "20s" frame
  }
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
  TEST_ASSERT_TRUE(store->hasValue);   // the source is kept
}

// The button cannot start a quarantined app: a tap does not run it.
void test_tap_does_not_run_a_quarantined_app(void) {
  Opts o; o.withButton = true;
  crashLimitTimes(o);
  sandbox->loop();
  button->down = true;  sandbox->loop();
  testMillis() += 100;  sandbox->loop();
  button->down = false; sandbox->loop();   // tap
  testMillis() += 30000; sandbox->loop();
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
  TEST_ASSERT_EQUAL_INT(0, telemetryCount("app_restored"));
  TEST_ASSERT_EQUAL_STRING(PAUSED_SCREEN, display->last().c_str());
}

// Nor does a long press run it, or forget it: only `forget` clears it.
void test_long_press_neither_runs_nor_forgets_a_quarantined_app(void) {
  Opts o; o.withButton = true;
  crashLimitTimes(o);
  sandbox->loop();
  button->down = true;  sandbox->loop();
  testMillis() += 1500; sandbox->loop();
  button->down = false; sandbox->loop();
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
  TEST_ASSERT_TRUE(store->hasQuarantine);
  TEST_ASSERT_TRUE(store->hasValue);
  TEST_ASSERT_EQUAL_STRING(PAUSED_SCREEN, display->last().c_str());
}

// forget clears it from the paused screen, and the next boot has nothing to
// restore or pause.
void test_forget_from_the_paused_screen(void) {
  Opts o; o.withButton = true;
  crashLimitTimes(o);
  sandbox->loop();
  sandbox->clearPersistedApp();
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  TEST_ASSERT_EQUAL_STRING("Device ID: a4cf1200\nType: native-test",
                           display->last().c_str());
  boot(ResetKind::PowerOn, o);
  sandbox->loop();
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
}

// A push of a different app clears it from the paused screen, and is what
// the next boot restores, through the ordinary countdown.
void test_different_push_from_the_paused_screen(void) {
  Opts o; o.withButton = true;
  crashLimitTimes(o);
  sandbox->loop();
  sandbox->loadApp(OTHER_APP);
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_FALSE(sandbox->isAppQuarantined());
  boot(ResetKind::PowerOn, o);
  sandbox->loop();
  testMillis() += 20000;
  sandbox->loop();
  TEST_ASSERT_TRUE(sandbox->isAppRunning());
  TEST_ASSERT_EQUAL_INT(1, telemetryCount("app_restored"));
}

void test_headless_quarantined_app_is_not_restored(void) {
  Opts o; o.withDisplay = false;
  crashLimitTimes(o);
  sandbox->loop();
  TEST_ASSERT_FALSE(sandbox->isAppRunning());
  TEST_ASSERT_TRUE(sandbox->isAppQuarantined());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_crashes_do_not_quarantine);
  RUN_TEST(test_third_crash_quarantines_and_persists_it);
  RUN_TEST(test_quarantine_survives_a_power_cycle);
  RUN_TEST(test_quarantined_app_is_not_restored);
  RUN_TEST(test_resending_the_quarantined_app_is_refused);
  RUN_TEST(test_a_different_app_runs_and_clears_the_quarantine);
  RUN_TEST(test_hello_reports_the_quarantine);
  RUN_TEST(test_forget_clears_the_quarantine);
  RUN_TEST(test_deliberate_restarts_do_not_count);
  RUN_TEST(test_shutdown_handler_disarms);
  RUN_TEST(test_clean_running_clears_the_count);
  RUN_TEST(test_armed_before_init_runs);
  RUN_TEST(test_failed_load_disarms);
  RUN_TEST(test_quarantined_app_gets_no_countdown);
  RUN_TEST(test_tap_does_not_run_a_quarantined_app);
  RUN_TEST(test_long_press_neither_runs_nor_forgets_a_quarantined_app);
  RUN_TEST(test_forget_from_the_paused_screen);
  RUN_TEST(test_different_push_from_the_paused_screen);
  RUN_TEST(test_headless_quarantined_app_is_not_restored);
  UNITY_END();
  return 0;
}
