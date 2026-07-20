// The mic streaming pump: while streaming, drains SystemMic each loop and ships
// frames to the sink. Works while the app is suspended.
#include <unity.h>
#include <vector>
#include "ResidentSandbox.cpp"

namespace {
class FakeMic : public Resident::SystemMic {
public:
  int frames = 512;
  const char* name() const override { return "mic"; }
  int read(int16_t* b, int maxSamples, int) override {
    for (int i = 0; i < maxSamples; i++) b[i] = (int16_t)i;
    return maxSamples;
  }
  uint32_t sampleRate() const override { return 16000; }
  int frameSamples() const override { return frames; }
};

FakeMic* mic = nullptr;
Resident::Sandbox* sandbox = nullptr;
std::vector<size_t> sends;

constexpr const char* APP =
    "function init(ctx) end\nfunction on_tick(ctx, dt) end\n";

void runLoop(int n) { for (int i = 0; i < n; i++) { testMillis() += 200; sandbox->loop(); } }
}  // namespace

void setUp(void) {
  testMillis() = 0;
  sends.clear();
  mic = new FakeMic();
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemMic = mic;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  sandbox->setMicStreamSink([](const uint8_t*, size_t len) { sends.push_back(len); return true; });
}
void tearDown(void) { delete sandbox; sandbox = nullptr; delete mic; mic = nullptr; }

void test_streaming_sends_frames_and_stops(void) {
  TEST_ASSERT_FALSE(sandbox->isMicStreaming());
  sandbox->startMicStream();
  TEST_ASSERT_TRUE(sandbox->isMicStreaming());
  runLoop(3);
  TEST_ASSERT_EQUAL_INT(3, (int)sends.size());
  TEST_ASSERT_EQUAL_UINT(512 * sizeof(int16_t), sends[0]);  // 1024 bytes

  sandbox->stopMicStream();
  runLoop(2);
  TEST_ASSERT_EQUAL_INT(3, (int)sends.size());              // no more
}

void test_streaming_works_while_suspended(void) {
  sandbox->loadApp(APP);
  sandbox->suspendApp();
  sandbox->startMicStream();
  runLoop(2);
  TEST_ASSERT_EQUAL_INT(2, (int)sends.size());
}

void test_frame_size_is_clamped(void) {
  mic->frames = 99999;                    // larger than internal buffer
  sandbox->startMicStream();
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, (int)sends.size());
  TEST_ASSERT_EQUAL_UINT(512 * sizeof(int16_t), sends[0]);  // clamped to 512
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_streaming_sends_frames_and_stops);
  RUN_TEST(test_streaming_works_while_suspended);
  RUN_TEST(test_frame_size_is_clamped);
  UNITY_END();
  return 0;
}
