// The outbound event queue (0.8): events.send returns "sent" | "queued" |
// "dropped"; rate-limited and offline sends queue (bounded, keep-aware) and
// drain in order from loop(). Plus the capture brackets: startCapture /
// endCapture wrap the mic stream in {system, capture} messages.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"

namespace {

class FakeMic : public Resident::SystemMic {
public:
  int beginCount = 0, endCount = 0;
  bool begin() override { beginCount++; return true; }
  void end() override { endCount++; }
  int read(int16_t* b, int maxSamples, int) override {
    (void)b; (void)maxSamples; return 0;
  }
  uint32_t sampleRate() const override { return 16000; }
  int frameSamples() const override { return 0; }
};

Resident::Sandbox* sandbox = nullptr;
FakeMic* mic = nullptr;
std::vector<std::string>* events = nullptr;
std::vector<std::string>* frames = nullptr;

void build(bool withMic = false) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.persistApps = false;
  if (withMic) cfg.systemMic = mic;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setSystemSink([](JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    frames->push_back(out);
    return true;
  });
  sandbox->setup();
}

void attachEventSink() {
  sandbox->setEventSink([](JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    events->push_back(out);
    return true;
  });
}

void loadApp(const char* code) {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = code;
  sandbox->injectMessage("test", "app", doc);
}

void pump(unsigned long ms = 200) { testMillis() += ms; sandbox->loop(); }

}  // namespace

void setUp(void) {
  testMillis() = 0;
  events = new std::vector<std::string>();
  frames = new std::vector<std::string>();
  mic = new FakeMic();
  sandbox = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete mic; mic = nullptr;
  delete events; events = nullptr;
  delete frames; frames = nullptr;
}

void test_send_returns_sent_with_sink_and_queued_without(void) {
  build();
  loadApp(
      "r1 = events.send('ping', { n = 1 })\n"
      "function on_tick(ctx, dt) end\n");
  // No event sink, no network: queued.
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("r1"));   // truthy ("queued")
  TEST_ASSERT_EQUAL_INT(0, (int)events->size());
  // Attach a sink; the next loop drains the queued event.
  attachEventSink();
  pump();
  TEST_ASSERT_EQUAL_INT(1, (int)events->size());
  TEST_ASSERT_TRUE(events->at(0).find("\"type\":\"ping\"") != std::string::npos);
}

void test_rate_limited_sends_queue_and_drain_in_order(void) {
  build();
  attachEventSink();
  loadApp(
      "sent, queued = 0, 0\n"
      "for i = 1, 12 do\n"
      "  local r = events.send('e' .. i, { i = i })\n"
      "  if r == 'sent' then sent = sent + 1\n"
      "  elseif r == 'queued' then queued = queued + 1 end\n"
      "end\n"
      "function on_tick(ctx, dt) end\n");
  // Burst of 10 tokens: 10 sent, 2 queued (none dropped).
  TEST_ASSERT_EQUAL_INT(10, sandbox->luaGlobalIntForTest("sent"));
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("queued"));
  TEST_ASSERT_EQUAL_INT(10, (int)events->size());
  // Tokens refill at 5/s: after a second, the two queued drain — in order.
  pump(1000);
  TEST_ASSERT_EQUAL_INT(12, (int)events->size());
  TEST_ASSERT_TRUE(events->at(10).find("\"type\":\"e11\"") != std::string::npos);
  TEST_ASSERT_TRUE(events->at(11).find("\"type\":\"e12\"") != std::string::npos);
}

void test_overflow_evicts_droppable_never_keepers(void) {
  build();
  // No sink: everything queues. Fill with 15 droppables + 1 keeper, then
  // push more droppables: evictions take droppables, the keeper survives.
  loadApp(
      "events.send('keeper', { k = 1 }, { keep = true })\n"
      "for i = 1, 15 do events.send('filler' .. i, {}) end\n"
      "for i = 1, 5 do events.send('extra' .. i, {}) end\n"
      "function on_tick(ctx, dt) end\n");
  attachEventSink();
  // Drain everything (token refills over a few seconds).
  for (int i = 0; i < 30; i++) pump(1000);
  std::string all;
  for (auto& e : *events) all += e;
  TEST_ASSERT_TRUE(all.find("\"type\":\"keeper\"") != std::string::npos);
  TEST_ASSERT_EQUAL_INT(16, (int)events->size());   // queue cap held
}

void test_full_of_keepers_drops_incoming_droppable(void) {
  build();
  loadApp(
      "for i = 1, 16 do events.send('k' .. i, {}, { keep = true }) end\n"
      "r = events.send('plain', {})\n"
      "dropped = (r == 'dropped')\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("dropped"));
}

void test_capture_brackets_wrap_the_mic_stream(void) {
  build(/*withMic=*/true);
  TEST_ASSERT_TRUE(sandbox->startCapture(1, 1));
  TEST_ASSERT_EQUAL_INT(1, mic->beginCount);
  sandbox->endCapture();
  TEST_ASSERT_EQUAL_INT(1, mic->endCount);
  // Two system frames: start (with stream+format), then end.
  const std::string* start = nullptr;
  const std::string* end = nullptr;
  for (auto& f : *frames) {
    if (f.find("\"type\":\"capture\"") == std::string::npos) continue;
    if (f.find("\"state\":\"start\"") != std::string::npos) start = &f;
    if (f.find("\"state\":\"end\"") != std::string::npos) end = &f;
  }
  TEST_ASSERT_NOT_NULL(start);
  TEST_ASSERT_TRUE(start->find("\"stream\":1") != std::string::npos);
  TEST_ASSERT_TRUE(start->find("\"format\":1") != std::string::npos);
  TEST_ASSERT_NOT_NULL(end);
}

void test_capture_without_mic_fails_closed(void) {
  build(/*withMic=*/false);
  TEST_ASSERT_FALSE(sandbox->startCapture());
  // The opened bracket is closed again: start then end, no stream left open.
  TEST_ASSERT_FALSE(sandbox->isMicStreaming());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_send_returns_sent_with_sink_and_queued_without);
  RUN_TEST(test_rate_limited_sends_queue_and_drain_in_order);
  RUN_TEST(test_overflow_evicts_droppable_never_keepers);
  RUN_TEST(test_full_of_keepers_drops_incoming_droppable);
  RUN_TEST(test_capture_brackets_wrap_the_mic_stream);
  RUN_TEST(test_capture_without_mic_fails_closed);
  return UNITY_END();
}
