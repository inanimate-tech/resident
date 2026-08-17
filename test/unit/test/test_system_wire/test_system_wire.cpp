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

void test_telemetry_queue_drops_oldest_when_unsendable(void) {
  build();
  // No sink yet: five bad loads queue 10 emissions into the 8-slot ring
  // (one slot kept free, 7 usable) — the oldest three fall off.
  for (int i = 0; i < 5; i++) loadApp("still not lua(");
  attachSink();
  pump();
  TEST_ASSERT_EQUAL_INT(7, (int)frames->size());
}

void test_hello_carries_identity_features_limits_and_app(void) {
  build("m5stick@2");
  attachSink();
  loadApp("function on_tick(ctx, dt) end", "g1", "oracle");
  frames->clear();
  sandbox->requestHello();
  pump();
  const std::string* f = frameWith("\"type\":\"hello\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"proto\":1") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"deviceType\":\"native-test\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"firmware\":\"9.9-test\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"profile\":\"m5stick@2\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"chunk\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"telemetry\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"eventBytes\":1024") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"storeBytes\":2048") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"storeNsChars\":32") != std::string::npos);
  // The running app: namespace + the wire-stamped generation id.
  TEST_ASSERT_TRUE(f->find("\"storeNs\":\"oracle\"") != std::string::npos);
  TEST_ASSERT_TRUE(f->find("\"generationId\":\"g1\"") != std::string::npos);
  // No mic configured: media is not claimed.
  TEST_ASSERT_TRUE(f->find("\"media\"") == std::string::npos);
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

void test_hello_omits_surfaces_when_registry_empty(void) {
  build();
  attachSink();
  sandbox->requestHello();
  pump();
  const std::string* f = frameWith("\"type\":\"hello\"");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_TRUE(f->find("\"surfaces\"") == std::string::npos);
}

void test_hello_carries_surfaces_from_registered_displays(void) {
  // A registered lgfx display reaches the hello through the render-target
  // registry, with its geometry, shape, and module list.
  NullTarget target;
  Resident::LgfxModule lgfx;
  lgfx.addDisplay("main", &target);            // default shape "rect"
  lgfx.addDisplay("dial", &target, "round");   // shape override
  // The lvgl module can't compile natively; feed its registry write directly
  // to prove same-name entries merge module bits into one surface.
  Resident::RenderTargets::add("main", 240, 135, nullptr,
                               Resident::RenderTargets::MODULE_LVGL);

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
  TEST_ASSERT_TRUE(f->find(
      "\"surfaces\":["
      "{\"name\":\"main\",\"w\":240,\"h\":135,\"shape\":\"rect\","
      "\"modules\":[\"lgfx\",\"lvgl\"]},"
      "{\"name\":\"dial\",\"w\":240,\"h\":135,\"shape\":\"round\","
      "\"modules\":[\"lgfx\"]}]") != std::string::npos);
}

void test_host_hello_marks_seen(void) {
  build();
  TEST_ASSERT_FALSE(sandbox->hostHelloSeen());
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "hello";
  doc["data"]["proto"] = 1;
  doc["data"]["tz"] = "Europe/London";
  sandbox->injectMessage("test", "hello", doc);
  TEST_ASSERT_TRUE(sandbox->hostHelloSeen());
}

void test_on_event_ctx_has_wallclock_fields(void) {
  build();
  loadApp(
      "function on_event(ctx, e)\n"
      "  saw_clock = (ctx.utc_h ~= nil) and (ctx.utc_m ~= nil)\n"
      "    and (ctx.localtime_h ~= nil) and (ctx.localtime_m ~= nil)\n"
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
  RUN_TEST(test_telemetry_queue_drops_oldest_when_unsendable);
  RUN_TEST(test_hello_carries_identity_features_limits_and_app);
  RUN_TEST(test_hello_without_app_omits_app_field);
  RUN_TEST(test_hello_omits_surfaces_when_registry_empty);
  RUN_TEST(test_hello_carries_surfaces_from_registered_displays);
  RUN_TEST(test_host_hello_marks_seen);
  RUN_TEST(test_on_event_ctx_has_wallclock_fields);
  return UNITY_END();
}
