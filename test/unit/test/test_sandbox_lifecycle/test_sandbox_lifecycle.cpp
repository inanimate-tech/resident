// Lifecycle + update-cadence + event-gating tests for the unified peripheral
// model. Networkless; spies count begin()/update() and dispatched events.
#include <unity.h>
#include <vector>
#include "ResidentSandbox.cpp"

namespace {

// A SystemButton spy usable as the systemButton slot (a peripheral) and/or an
// extension. Counts begin()/update(); pressed() returns a settable level.
class SpyButton : public Resident::SystemButton {
public:
  int beginCount = 0, updateCount = 0;
  bool down = false;
  const char* name() const override { return "spy-button"; }
  void begin() override { beginCount++; }
  void update() override { updateCount++; }
  bool pressed() override { return down; }
};

// A plain Driver spy (a non-peripheral extension). Counts begin()/update().
class SpyDriver : public Resident::Driver {
public:
  int beginCount = 0, updateCount = 0;
  const char* name() const override { return "spy-driver"; }
  void begin() override { beginCount++; }
  void update() override { updateCount++; }
};

// A StatusDisplay spy usable as the statusDisplay slot and/or an extension.
// Counts begin()/update(); displayText() is a no-op.
class SpyDisplay : public Resident::StatusDisplay {
public:
  int beginCount = 0, updateCount = 0;
  const char* name() const override { return "spy-display"; }
  void begin() override { beginCount++; }
  void update() override { updateCount++; }
  void displayText(const char*) override {}
};

SpyButton* button = nullptr;
SpyDriver* driver = nullptr;
SpyDisplay* display = nullptr;
Resident::Sandbox* sandbox = nullptr;

constexpr const char* APP =
    "function init(ctx) end\n"
    "function on_tick(ctx, dt) end\n";

}  // namespace

void setUp(void) {
  testMillis() = 0;
  button = new SpyButton();
  driver = new SpyDriver();
  display = new SpyDisplay();
  sandbox = nullptr;
}
void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete button; button = nullptr;
  delete driver; driver = nullptr;
  delete display; display = nullptr;
}

void test_begin_once_when_in_both_list_and_slot(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {button};          // button in the extension list...
  cfg.systemButton = button;          // ...and assigned as the system button
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  TEST_ASSERT_EQUAL_INT(1, button->beginCount);   // de-duped: begun once
}

// Guard against re-introducing the direct statusDisplay->begin() call that
// existed before Task 2. If that call is re-added, beginCount becomes 2.
void test_begin_once_when_display_in_both_list_and_slot(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {display};         // display in the extension list...
  cfg.statusDisplay = display;        // ...and assigned as the status display
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  TEST_ASSERT_EQUAL_INT(1, display->beginCount);  // de-duped: begun once
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_once_when_in_both_list_and_slot);
  RUN_TEST(test_begin_once_when_display_in_both_list_and_slot);
  UNITY_END();
  return 0;
}
