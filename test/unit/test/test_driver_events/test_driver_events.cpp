// One payload shape (0.8): driver events deliver their fields as e.data,
// through the same ArduinoJson parse as wire events — the flattened shape
// (and its hand-rolled parser that dropped booleans and nesting) is gone.
// EventField grows FLOAT and BOOL alongside INT and STRING.
#include <unity.h>

#include "ResidentSandbox.cpp"

namespace {

class ProbeDriver : public Resident::Driver {
public:
  const char* name() const override { return "probe"; }
  void registerModule(Resident::LuaModule&) override {}
  void begin() override {}
  void update() override {}

  void emitAll() {
    Resident::EventField fields[] = {
        {"index", Resident::EventField::INT, {.i = 2}},
        {"label", Resident::EventField::STRING, {.s = "hi"}},
        {"mag", Resident::EventField::FLOAT, {.f = 1.5f}},
        {"held", Resident::EventField::BOOL, {.b = true}},
    };
    sendEvent("probe", fields, 4);
  }

  void emitButton(int index) {
    Resident::EventField fields[] = {
        {"index", Resident::EventField::INT, {.i = index}},
    };
    sendEvent("button", fields, 1);
  }
};

ProbeDriver* driver = nullptr;
Resident::Sandbox* sandbox = nullptr;

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.extensions = {driver};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}

void loadApp(const char* code) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  sandbox->injectMessage("test", "app", doc);
}

void pump() { testMillis() += 200; sandbox->loop(); }

}  // namespace

void setUp(void) {
  testMillis() = 0;
  driver = new ProbeDriver();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete driver; driver = nullptr;
}

void test_driver_event_delivers_data_table_with_all_field_types(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  got_name = e.name\n"
      "  got_channel = e.channel\n"
      "  got_index = e.data.index\n"
      "  str_ok = (e.data.label == 'hi')\n"
      "  mag_ok = (e.data.mag > 1.49 and e.data.mag < 1.51)\n"
      "  held_ok = (e.data.held == true)\n"
      "  no_flatten = (e.index == nil) and (e.label == nil)\n"
      "end\n");
  driver->emitAll();
  pump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("got_index"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("str_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("mag_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("held_ok"));
  // The fields arrive ONLY under e.data — nothing is flattened anymore.
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("no_flatten"));
}

void test_driver_event_channel_is_driver(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  is_driver = (e.channel == 'driver')\n"
      "end\n");
  driver->emitButton(0);
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("is_driver"));
}

void test_button_events_still_count_triggers(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  triggers = ctx.trigger_count\n"
      "end\n");
  driver->emitButton(0);
  pump();
  driver->emitButton(1);
  pump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("triggers"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_driver_event_delivers_data_table_with_all_field_types);
  RUN_TEST(test_driver_event_channel_is_driver);
  RUN_TEST(test_button_events_still_count_triggers);
  return UNITY_END();
}
