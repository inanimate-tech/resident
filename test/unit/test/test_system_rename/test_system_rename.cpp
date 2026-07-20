// Verifies the status→system rename: a SystemDisplay assigned to the new
// cfg.systemDisplay role slot updates every loop as a peripheral (even with no
// app loaded), and the deprecated cfg.statusDisplay alias routes identically.
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
class SpyDisplay : public Resident::SystemDisplay {
public:
  int updates = 0;
  const char* name() const override { return "display"; }
  void update() override { updates++; }
  void displayText(const char* t) override { (void)t; }
};

SpyDisplay* disp = nullptr;
Resident::Sandbox* sandbox = nullptr;

void runLoop(int n) {
  for (int i = 0; i < n; i++) { testMillis() += 200; sandbox->loop(); }
}
}  // namespace

void tearDown(void) { delete sandbox; sandbox = nullptr; delete disp; disp = nullptr; }
void setUp(void) { testMillis() = 0; disp = new SpyDisplay(); }

void test_system_display_updates_as_peripheral(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemDisplay = disp;                 // NEW field
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  runLoop(3);
  TEST_ASSERT_EQUAL_INT(3, disp->updates);  // peripheral cadence, no app
}

void test_deprecated_status_display_alias_routes(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  cfg.statusDisplay = disp;                 // DEPRECATED field still works
#pragma GCC diagnostic pop
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  runLoop(3);
  TEST_ASSERT_EQUAL_INT(3, disp->updates);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_system_display_updates_as_peripheral);
  RUN_TEST(test_deprecated_status_display_alias_routes);
  UNITY_END();
  return 0;
}
