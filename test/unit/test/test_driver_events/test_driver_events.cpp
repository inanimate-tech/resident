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
  void registerModule(Resident::LuaModule& m) override {
    m.method<ProbeDriver, &ProbeDriver::burn>("burn")
     .method<ProbeDriver, &ProbeDriver::again>("again");
  }

  // probe.burn(): a handler that takes 15 ms of (test) clock.
  int burn(lua_State*) { testMillis() += 15; return 0; }
  // probe.again(): a handler that queues another event while it runs.
  int again(lua_State*) { emitButton(9); return 0; }
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

// One loop() pass dispatches every queued event, not one: a tap is two
// events, and each used to wait a pass behind the one ahead of it.
void test_one_pass_dispatches_every_queued_event(void) {
  build();
  loadApp(
      "n = 0\n"
      "function on_event(ctx, e) n = n + 1 end\n");
  for (int i = 0; i < 5; i++) driver->emitButton(i);
  pump();
  TEST_ASSERT_EQUAL_INT(5, sandbox->luaGlobalIntForTest("n"));
}

// The drain stops once RESIDENT_EVENT_DRAIN_BUDGET_MS (20) has gone: 15 ms
// handlers fit one, then a second that crosses the budget, and the rest wait
// for the next passes in order.
void test_drain_stops_at_the_budget(void) {
  build();
  loadApp(
      "n = 0\n"
      "last = -1\n"
      "function on_event(ctx, e)\n"
      "  n = n + 1\n"
      "  last = e.data.index\n"
      "  probe.burn()\n"
      "end\n");
  for (int i = 0; i < 5; i++) driver->emitButton(i);
  pump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("n"));
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("last"));
  pump();
  TEST_ASSERT_EQUAL_INT(4, sandbox->luaGlobalIntForTest("n"));
  pump();
  TEST_ASSERT_EQUAL_INT(5, sandbox->luaGlobalIntForTest("n"));
  TEST_ASSERT_EQUAL_INT(4, sandbox->luaGlobalIntForTest("last"));
}

// An event a handler queues while it runs waits for the next pass, so a
// handler that feeds the ring cannot keep one pass draining.
void test_events_queued_by_a_handler_wait_for_the_next_pass(void) {
  build();
  loadApp(
      "n = 0\n"
      "function on_event(ctx, e)\n"
      "  n = n + 1\n"
      "  probe.again()\n"
      "end\n");
  driver->emitButton(0);
  pump();
  TEST_ASSERT_EQUAL_INT(1, sandbox->luaGlobalIntForTest("n"));
  pump();
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("n"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_driver_event_delivers_data_table_with_all_field_types);
  RUN_TEST(test_driver_shadow_never_clobbers_envelope_keys);
  RUN_TEST(test_wire_events_get_no_shadow);
  RUN_TEST(test_driver_event_channel_is_driver);
  RUN_TEST(test_ctx_has_no_trigger_count);
  RUN_TEST(test_one_pass_dispatches_every_queued_event);
  RUN_TEST(test_drain_stops_at_the_budget);
  RUN_TEST(test_events_queued_by_a_handler_wait_for_the_next_pass);
  return UNITY_END();
}
