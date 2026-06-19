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

void test_peripheral_updates_without_app_but_plain_driver_does_not(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {button, driver};   // button is also the system button
  cfg.systemButton = button;           // -> peripheral
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();                     // no app loaded -> RunState::Ready

  int b0 = button->updateCount, d0 = driver->updateCount;
  sandbox->loop();
  TEST_ASSERT_EQUAL_INT(b0 + 1, button->updateCount);   // peripheral: always
  TEST_ASSERT_EQUAL_INT(d0,     driver->updateCount);    // plain driver: not yet

  sandbox->loadApp(APP);                // app loaded -> Running
  int b1 = button->updateCount, d1 = driver->updateCount;
  sandbox->loop();
  TEST_ASSERT_EQUAL_INT(b1 + 1, button->updateCount);   // still updates
  TEST_ASSERT_EQUAL_INT(d1 + 1, driver->updateCount);    // now updates too
}

void test_dual_role_object_updates_once_per_loop(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {button};            // in the list...
  cfg.systemButton = button;            // ...and the slot (peripheral)
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  sandbox->loadApp(APP);                // Running: both cadences would apply

  int b0 = button->updateCount;
  sandbox->loop();
  TEST_ASSERT_EQUAL_INT(b0 + 1, button->updateCount);   // once, not twice
}

class EventSpy : public Resident::Driver {
public:
  int fired = 0;
  const char* name() const override { return "probe"; }
  void registerModule(Resident::LuaModule& m) override {
    m.method<EventSpy, &EventSpy::luaFired>("fired");
  }
  int luaFired(lua_State*) { fired++; return 0; }
  void emit() {
    Resident::EventField f[] = {{"v", Resident::EventField::INT, {.i = 1}}};
    sendEvent("ping", f, 1);
  }
};

void test_driver_event_dropped_until_app_loaded(void) {
  EventSpy* spy = new EventSpy();
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {spy};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();                       // Ready, no app

  spy->emit();                            // event while no app is loaded
  const char* APP_EV =
      "function on_tick(ctx, dt) end\n"
      "function on_event(ctx, e) probe.fired() end\n";
  sandbox->loadApp(APP_EV);               // now Running
  testMillis() += 200; sandbox->loop();   // dispatch a queued event, if any
  TEST_ASSERT_EQUAL_INT(0, spy->fired);   // the Ready-time event was dropped

  spy->emit();                            // event while Running
  testMillis() += 200; sandbox->loop();
  TEST_ASSERT_EQUAL_INT(1, spy->fired);   // delivered
  delete spy;
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_once_when_in_both_list_and_slot);
  RUN_TEST(test_begin_once_when_display_in_both_list_and_slot);
  RUN_TEST(test_peripheral_updates_without_app_but_plain_driver_does_not);
  RUN_TEST(test_dual_role_object_updates_once_per_loop);
  RUN_TEST(test_driver_event_dropped_until_app_loaded);
  UNITY_END();
  return 0;
}
