// The outbound control plane: the device hello and wire-borne telemetry.
// Both queue (sends must never happen from the receive context) and drain
// from loop() through sendSystem — captured here via the system sink, the
// control-plane mirror of setEventSink. Plus the ctx-uniformity fix: the
// wall-clock fields reach on_event like every other callback.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"
#include "ResidentLgfxModule.h"

namespace {

Resident::Sandbox* sandbox = nullptr;
std::vector<std::string>* frames = nullptr;
std::vector<std::string>* boardFrames = nullptr;   // the telemetry callback's

// A do-nothing LgfxTarget with real geometry — enough to feed the
// render-target registry via the lgfx module's addDisplay path.
struct NullTarget : Resident::LgfxTarget {
  void fillScreen(uint32_t) override {}
  void drawPixel(int32_t, int32_t, uint32_t) override {}
  void drawLine(int32_t, int32_t, int32_t, int32_t, uint32_t) override {}
  void drawRect(int32_t, int32_t, int32_t, int32_t, uint32_t) override {}
  void fillRect(int32_t, int32_t, int32_t, int32_t, uint32_t) override {}
  void drawRoundRect(int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t) override {}
  void fillRoundRect(int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t) override {}
  void drawCircle(int32_t, int32_t, int32_t, uint32_t) override {}
  void fillCircle(int32_t, int32_t, int32_t, uint32_t) override {}
  void drawTriangle(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t) override {}
  void fillTriangle(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t) override {}
  void setTextColor(uint32_t, uint32_t, bool) override {}
  void setTextSize(float) override {}
  void setTextDatum(uint8_t) override {}
  void setCursor(int32_t, int32_t) override {}
  void print(const char*) override {}
  void drawString(const char*, int32_t, int32_t) override {}
  int32_t width() override { return 240; }
  int32_t height() override { return 135; }
};

void build(const char* profileRef = nullptr) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.profileRef = profileRef;
  cfg.firmwareVersion = "9.9-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}

void attachSink() {
  sandbox->setSystemSink([](JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    frames->push_back(out);
    return true;
  });
}

bool frameHas(const char* needle) {
  for (auto& f : *frames)
    if (f.find(needle) != std::string::npos) return true;
  return false;
}

const std::string* frameWith(const char* needle) {
  for (auto& f : *frames)
    if (f.find(needle) != std::string::npos) return &f;
  return nullptr;
}

void loadApp(const char* code, const char* generationId = nullptr,
             const char* storeNs = nullptr) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  if (generationId) doc["generationId"] = generationId;
  if (storeNs) doc["storeNs"] = storeNs;
  sandbox->injectMessage("test", "app", doc);
}

void pump() { testMillis() += 200; sandbox->loop(); }

}  // namespace

void setUp(void) {
  testMillis() = 0;
  frames = new std::vector<std::string>();
  sandbox = nullptr;
  // The registry is board-lifetime (static) — isolate tests from each other.
  Resident::RenderTargets::clear();
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete frames; frames = nullptr;
  delete boardFrames; boardFrames = nullptr;
}

void test_telemetry_rides_the_wire(void) {
  build();
  attachSink();
  loadApp("this is not lua(");
  pump();
  // Both emissions from the load arrive as channelled system telemetry.
  TEST_ASSERT_TRUE(frameHas("\"name\":\"app_received\""));
  const std::string* f = frameWith("\"name\":\"compile_error\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"channel\":\"system\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"type\":\"telemetry\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"error\":") != std::string::npos);
}

bool hasRawControlByte(const std::string& s) {
  for (unsigned char c : s) {
    if (c < 0x20) return true;
  }
  return false;
}

void captureBoardTelemetry() {
  boardFrames = new std::vector<std::string>();
  sandbox->setTelemetryCallback([](const char* json) {
    boardFrames->push_back(json ? json : "");
  });
}

const std::string* boardFrameWith(const char* needle) {
  for (auto& s : *boardFrames)
    if (s.find(needle) != std::string::npos) return &s;
  return nullptr;
}

// An app's text reaches telemetry (log.error, runtime errors), and the host
// stamps the generation id: neither may put a raw control byte in a frame,
// on the wire or in the board callback's flat copy.
void test_telemetry_frames_never_carry_raw_control_bytes(void) {
  build();
  attachSink();
  captureBoardTelemetry();
  loadApp("function init(ctx) log.error('bad\\1thing \"q\" \\\\ \\31') end\n"
          "function on_tick(ctx, dt) end\n",
          "g\x01" "1");
  pump();
  const std::string* f = frameWith("\"name\":\"log_error\"");
  TEST_ASSERT_NOT_NULL(f);
  for (auto& w : *frames) TEST_ASSERT_FALSE(hasRawControlByte(w));
  TEST_ASSERT_TRUE(f->find("\"error\":\"bad\xEF\xBF\xBDthing \\\"q\\\" \\\\ "
                           "\xEF\xBF\xBD\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"generationId\":\"g\xEF\xBF\xBD" "1\"") !=
                   std::string::npos);

  const std::string* b = boardFrameWith("\"name\":\"log_error\"");
  TEST_ASSERT_NOT_NULL(b);
  for (auto& s : *boardFrames) TEST_ASSERT_FALSE(hasRawControlByte(s));
  JsonDocument parsed;
  TEST_ASSERT_FALSE(deserializeJson(parsed, *b));
  TEST_ASSERT_EQUAL_STRING("bad\xEF\xBF\xBDthing \"q\" \\ \xEF\xBF\xBD",
                           parsed["data"]["error"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("g\xEF\xBF\xBD" "1",
                           parsed["generationId"].as<const char*>());
}

// The board callback's copy is bounded: a long error is cut, never inside a
// character, and the frame stays valid JSON.
void test_telemetry_callback_cuts_a_long_error_at_a_character(void) {
  build();
  captureBoardTelemetry();
  // 'a' then 300 two-byte characters: byte 256 is the middle of one.
  loadApp("function init(ctx) log.error('a' .. string.rep('\\195\\169', 300)) end\n"
          "function on_tick(ctx, dt) end\n");
  const std::string* b = boardFrameWith("\"name\":\"log_error\"");
  TEST_ASSERT_NOT_NULL(b);
  JsonDocument parsed;
  TEST_ASSERT_FALSE(deserializeJson(parsed, *b));
  TEST_ASSERT_EQUAL_INT(255, (int)strlen(parsed["data"]["error"].as<const char*>()));
}

void test_telemetry_queue_drops_oldest_when_unsendable(void) {
  build();
  // No sink yet: five bad loads queue 10 emissions into the 8-slot ring
  // (one slot kept free, 7 usable) — the oldest three fall off.
  for (int i = 0; i < 5; i++) loadApp("still not lua(");
  attachSink();
  pump();
  TEST_ASSERT_EQUAL_INT(7, (int)frames->size());
}

void test_hello_carries_identity_limits_and_app(void) {
  build("m5stick@2");
  attachSink();
  loadApp("function on_tick(ctx, dt) end", "g1", "oracle");
  frames->clear();
  sandbox->requestHello();
  pump();
  const std::string* f = frameWith("\"type\":\"hello\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"protocol\":1") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"deviceType\":\"native-test\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"firmware\":\"9.9-test\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"profile\":\"m5stick@2\"") != std::string::npos);
  // No feature list: proto carries compatibility; capture announces itself
  // via its bracket; telemetry needs no advance notice.
  TEST_ASSERT_TRUE(f->find("\"features\"") == std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"eventBytes\":1024") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"eventNameBytes\":31") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"storeBytes\":2048") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"storeNsChars\":32") != std::string::npos);
  // The running app: namespace + the wire-stamped generation id.
  TEST_ASSERT_TRUE(f->find("\"storeNs\":\"oracle\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"generationId\":\"g1\"") != std::string::npos);
}

void test_hello_without_app_omits_app_field(void) {
  build();
  attachSink();
  sandbox->requestHello();
  pump();
  const std::string* f = frameWith("\"type\":\"hello\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"app\"") == std::string::npos);
  // A restored-then-running app with a SELF-generated generation id must not
  // announce it (it means nothing to the host).
  loadApp("function on_tick(ctx, dt) end");   // no generationId stamped
  frames->clear();
  sandbox->requestHello();
  pump();
  f = frameWith("\"type\":\"hello\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"storeNs\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"generationId\"") == std::string::npos);
}

void test_hello_never_carries_authoring_facts(void) {
  // Surfaces, sensors, and driver modules are AUTHORING facts — their one
  // authority is the document behind `profile`. Even with displays
  // registered, the hello stays session-mechanics-only.
  NullTarget target;
  Resident::LgfxModule lgfx;
  lgfx.addDisplay("main", &target);
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  cfg.extensions = {&lgfx};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  attachSink();
  sandbox->requestHello();
  pump();
  const std::string* f = frameWith("\"type\":\"hello\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"surfaces\"") == std::string::npos);
}

void test_render_target_registry_merges_and_serves_bind(void) {
  // The registry stays as INTERNAL machinery: both graphics modules feed
  // it, same-name entries merge module bits, geometry comes from the
  // target — it just doesn't broadcast.
  NullTarget target;
  Resident::LgfxModule lgfx;
  lgfx.addDisplay("main", &target);            // default shape "rect"
  lgfx.addDisplay("dial", &target, "round");   // shape override
  Resident::RenderTargets::add("main", 240, 135, nullptr,
                               Resident::RenderTargets::MODULE_LVGL);
  TEST_ASSERT_EQUAL_INT(2, Resident::RenderTargets::count());
  const auto& main0 = Resident::RenderTargets::entry(0);
  TEST_ASSERT_EQUAL_STRING("main", main0.name);
  TEST_ASSERT_EQUAL_INT(240, main0.w);
  TEST_ASSERT_EQUAL_INT(135, main0.h);
  TEST_ASSERT_EQUAL_STRING("rect", main0.shape);
  TEST_ASSERT_TRUE(main0.modules & Resident::RenderTargets::MODULE_LGFX);
  TEST_ASSERT_TRUE(main0.modules & Resident::RenderTargets::MODULE_LVGL);
  const auto& dial = Resident::RenderTargets::entry(1);
  TEST_ASSERT_EQUAL_STRING("round", dial.shape);
  TEST_ASSERT_FALSE(dial.modules & Resident::RenderTargets::MODULE_LVGL);
}

void test_host_hello_marks_seen(void) {
  build();
  TEST_ASSERT_FALSE(sandbox->hostHelloSeen());
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "hello";
  doc["data"]["protocol"] = 1;
  doc["data"]["tz"] = "Europe/London";
  sandbox->injectMessage("test", "hello", doc);
  TEST_ASSERT_TRUE(sandbox->hostHelloSeen());
}

// The wall clock is the datetime module's (datetime.now()), not ctx's: the
// utc_h/utc_m/localtime_h/localtime_m fields went with the shader templates.
// ctx is the dispatch's own facts — time_ms since the app loaded.
void test_on_event_ctx_carries_no_wallclock_fields(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  saw_clock = (ctx.utc_h == nil) and (ctx.utc_m == nil)\n"
      "    and (ctx.localtime_h == nil) and (ctx.localtime_m == nil)\n"
      "    and (ctx.time_ms ~= nil)\n"
      "end\n");
  JsonDocument evt;
  evt["channel"] = "app";
  evt["type"] = "ping";
  evt["data"].to<JsonObject>();
  sandbox->injectMessage("test", "ping", evt);
  pump();
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("saw_clock"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_telemetry_rides_the_wire);
  RUN_TEST(test_telemetry_frames_never_carry_raw_control_bytes);
  RUN_TEST(test_telemetry_callback_cuts_a_long_error_at_a_character);
  RUN_TEST(test_telemetry_queue_drops_oldest_when_unsendable);
  RUN_TEST(test_hello_carries_identity_limits_and_app);
  RUN_TEST(test_hello_without_app_omits_app_field);
  RUN_TEST(test_hello_never_carries_authoring_facts);
  RUN_TEST(test_render_target_registry_merges_and_serves_bind);
  RUN_TEST(test_host_hello_marks_seen);
  RUN_TEST(test_on_event_ctx_carries_no_wallclock_fields);
  return UNITY_END();
}
