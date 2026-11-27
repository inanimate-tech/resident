// The boot gesture: the system button held through boot for
// SandboxConfig::wifiPortalHoldMs asks Courier for the setup portal, and the
// press starts no runtime hold until it is released.
#include <unity.h>
#include <string>
#include <vector>
#include "ResidentSandbox.cpp"

namespace {

// Held for heldForMs of the fake clock, read by read: setup() waits on
// pressed() with delay(), which moves the clock on.
class TimedButton : public Resident::SystemButton {
public:
  unsigned long heldForMs = 0;
  const char* name() const override { return "sysbtn"; }
  bool pressed() override { return testMillis() < heldForMs; }
};

class SpyDisplay : public Resident::SystemDisplay {
public:
  std::vector<std::string> shown;
  const char* name() const override { return "spy-display"; }
  void displayText(const char* text) override { shown.push_back(text); }
};

TimedButton* btn = nullptr;
SpyDisplay* display = nullptr;
Resident::Sandbox* sandbox = nullptr;
std::vector<bool> holds;

Resident::SandboxConfig config(bool networked) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemButton = btn;
  cfg.systemDisplay = display;
  cfg.persistApps = false;
  if (networked) {
    Courier::Config courier;
    courier.host = "example.com";
    cfg.network = courier;
  }
  return cfg;
}

int portalRequests() { return sandbox->courier().openConfigPortalCalls; }

bool shownText(const char* text) {
  for (const auto& s : display->shown) if (s == text) return true;
  return false;
}

}  // namespace

void setUp(void) {
  testMillis() = 0;
  holds.clear();
  btn = new TimedButton();
  display = new SpyDisplay();
}
void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete btn; btn = nullptr;
  delete display; display = nullptr;
}

void test_held_through_boot_opens_the_portal(void) {
  btn->heldForMs = 10000;
  sandbox = new Resident::Sandbox(config(true));
  sandbox->setup();
  TEST_ASSERT_EQUAL_INT(1, portalRequests());
  TEST_ASSERT_TRUE(shownText("Keep holding\nfor WiFi setup"));
  // setup() returned at the threshold, not at the release.
  TEST_ASSERT_TRUE(testMillis() >= 3000 && testMillis() < 3100);
}

void test_released_early_boots_as_usual(void) {
  btn->heldForMs = 2000;
  sandbox = new Resident::Sandbox(config(true));
  sandbox->setup();
  TEST_ASSERT_EQUAL_INT(0, portalRequests());
  TEST_ASSERT_TRUE(testMillis() >= 2000 && testMillis() < 2100);
}

void test_not_held_does_not_wait(void) {
  sandbox = new Resident::Sandbox(config(true));
  sandbox->setup();
  TEST_ASSERT_EQUAL_INT(0, portalRequests());
  TEST_ASSERT_EQUAL_UINT32(0, testMillis());
  TEST_ASSERT_FALSE(shownText("Keep holding\nfor WiFi setup"));
}

void test_zero_turns_it_off(void) {
  btn->heldForMs = 10000;
  Resident::SandboxConfig cfg = config(true);
  cfg.wifiPortalHoldMs = 0;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  TEST_ASSERT_EQUAL_INT(0, portalRequests());
  TEST_ASSERT_EQUAL_UINT32(0, testMillis());
}

void test_standalone_does_not_wait(void) {
  btn->heldForMs = 10000;
  sandbox = new Resident::Sandbox(config(false));
  sandbox->setup();
  TEST_ASSERT_EQUAL_UINT32(0, testMillis());
}

void test_boot_press_starts_no_runtime_hold_until_released(void) {
  btn->heldForMs = 10000;
  sandbox = new Resident::Sandbox(config(true));
  sandbox->setup();
  sandbox->onSystemButtonHold([](bool held) { holds.push_back(held); });

  // Still held well past the runtime threshold: no hold.
  for (int i = 0; i < 20; i++) { testMillis() += 100; sandbox->loop(); }
  TEST_ASSERT_EQUAL_INT(0, (int)holds.size());

  // Released, then a fresh press is a hold as usual.
  testMillis() = 10000;
  sandbox->loop();
  btn->heldForMs = 20000;
  testMillis() += 10; sandbox->loop();
  testMillis() += 600; sandbox->loop();
  TEST_ASSERT_EQUAL_INT(1, (int)holds.size());
  TEST_ASSERT_TRUE(holds[0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_held_through_boot_opens_the_portal);
  RUN_TEST(test_released_early_boots_as_usual);
  RUN_TEST(test_not_held_does_not_wait);
  RUN_TEST(test_zero_turns_it_off);
  RUN_TEST(test_standalone_does_not_wait);
  RUN_TEST(test_boot_press_starts_no_runtime_hold_until_released);
  return UNITY_END();
}
