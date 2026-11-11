// One payload shape (0.8): driver events deliver their fields as e.data,
// through the same ArduinoJson parse as wire events — the hand-rolled
// flatten parser (dropped booleans and nesting) is gone. For one
// deprecation window driver events ALSO mirror their top-level scalars
// onto the event table (the historical flattened shape), envelope keys
// excepted, so existing apps survive a firmware bump unchanged.
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

  void emitEnvelopeCollision() {
    Resident::EventField fields[] = {
        {"name", Resident::EventField::STRING, {.s = "evil"}},
        {"channel", Resident::EventField::STRING, {.s = "system"}},
    };
    sendEvent("collide", fields, 2);
  }

  void emitButton(int index) {
    Resident::EventField fields[] = {
        {"index", Resident::EventField::INT, {.i = index}},
    };
    sendEvent("button", fields, 1);
  }

  // A string a driver passes through from outside (a network name, a
  // scanned tag): it must reach the app intact, quotes and all.
  void emitAwkwardString() {
    Resident::EventField fields[] = {
        {"index", Resident::EventField::INT, {.i = 1}},
        {"text", Resident::EventField::STRING, {.s = "say \"hi\" \\ \n"}},
    };
    sendEvent("awkward", fields, 2);
  }

  // A field too big for the data buffer is left out whole, with those after.
  void emitOversizeField() {
    static std::string big(RESIDENT_EVENT_JSON_MAX, 'z');
    Resident::EventField fields[] = {
        {"index", Resident::EventField::INT, {.i = 3}},
        {"big", Resident::EventField::STRING, {.s = big.c_str()}},
        {"after", Resident::EventField::INT, {.i = 4}},
    };
    sendEvent("oversize", fields, 3);
  }

  // JSON has no NaN or infinity: such a float arrives as nil, and the
  // event with it.
  void emitNonFinite() {
    Resident::EventField fields[] = {
        {"index", Resident::EventField::INT, {.i = 5}},
        {"nan", Resident::EventField::FLOAT, {.f = NAN}},
        {"inf", Resident::EventField::FLOAT, {.f = INFINITY}},
        {"mag", Resident::EventField::FLOAT, {.f = 2.5f}},
    };
    sendEvent("nonfinite", fields, 4);
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
      "  shadow_ok = (e.index == 2) and (e.label == 'hi')\n"
      "    and (e.mag > 1.49 and e.mag < 1.51) and (e.held == true)\n"
      "end\n");
  driver->emitAll();
  pump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("got_index"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("str_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("mag_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("held_ok"));
  // The deprecated compatibility shadow: the same scalars ALSO sit on the
  // event table, so pre-0.8 apps survive a firmware bump unchanged.
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("shadow_ok"));
}

void test_driver_shadow_never_clobbers_envelope_keys(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  name_ok = (e.name == 'collide')\n"
      "  channel_ok = (e.channel == 'driver')\n"
      "  data_ok = (e.data.name == 'evil') and (e.data.channel == 'system')\n"
      "end\n");
  driver->emitEnvelopeCollision();
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("name_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("channel_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("data_ok"));
}

void test_wire_events_get_no_shadow(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  data_ok = (e.data.index == 7)\n"
      "  no_shadow = (e.index == nil)\n"
      "end\n");
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "poke";
  evt["data"]["index"] = 7;
  sandbox->injectMessage("test", "poke", evt);
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("data_ok"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("no_shadow"));
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

void test_driver_string_field_reaches_lua_intact(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  text_ok = (e.data.text == 'say \"hi\" \\\\ \\n') and (e.data.index == 1)\n"
      "end\n");
  driver->emitAwkwardString();
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("text_ok"));
}

void test_driver_field_too_big_is_left_out_whole(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  kept = (e.data.index == 3) and (e.data.big == nil)\n"
      "    and (e.data.after == nil)\n"
      "end\n");
  driver->emitOversizeField();
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("kept"));
}

void test_driver_non_finite_float_arrives_as_nil(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  nf = (e.data.index == 5) and (e.data.nan == nil)\n"
      "    and (e.data.inf == nil) and (e.data.mag == 2.5)\n"
      "end\n");
  driver->emitNonFinite();
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("nf"));
}

// ctx.trigger_count (a button-press counter for the retired shader templates)
// is gone: a button is an event, and an app counts its own.
void test_ctx_has_no_trigger_count(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  absent = (ctx.trigger_count == nil)\n"
      "end\n");
  driver->emitButton(0);
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("absent"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_driver_event_delivers_data_table_with_all_field_types);
  RUN_TEST(test_driver_shadow_never_clobbers_envelope_keys);
  RUN_TEST(test_wire_events_get_no_shadow);
  RUN_TEST(test_driver_event_channel_is_driver);
  RUN_TEST(test_driver_string_field_reaches_lua_intact);
  RUN_TEST(test_driver_field_too_big_is_left_out_whole);
  RUN_TEST(test_driver_non_finite_float_arrives_as_nil);
  RUN_TEST(test_ctx_has_no_trigger_count);
  return UNITY_END();
}
