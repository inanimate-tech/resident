// The mic streaming pump: while streaming, drains SystemMic each loop and ships
// frames to the sink. Works while the app is suspended.
#include <unity.h>
#include <vector>
#include "ResidentSandbox.cpp"

namespace {
class FakeMic : public Resident::SystemMic {
public:
  int frames = 512;
  int returnSamples = -1;    // -1 = fill maxSamples; otherwise a short read
  int lastTimeoutMs = -999;  // what the pump asked for
  int calls = 0;
  const char* name() const override { return "mic"; }
  int read(int16_t* b, int maxSamples, int timeoutMs) override {
    lastTimeoutMs = timeoutMs;
    calls++;
    int n = returnSamples < 0 ? maxSamples : returnSamples;
    if (n > maxSamples) n = maxSamples;
    for (int i = 0; i < n; i++) b[i] = (int16_t)i;
    return n;
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

// SystemMic contract: the pump runs inside loop(), so it must never ask the
// driver to block. Drivers are documented against this (ResidentSystemMic.h);
// if the pump ever starts passing a non-zero timeout, they are entitled to
// stall the sandbox for it.
void test_pump_never_asks_the_driver_to_block(void) {
  sandbox->startMicStream();
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, mic->calls);
  TEST_ASSERT_EQUAL_INT(0, mic->lastTimeoutMs);
}

// Short reads are legal: forward exactly what read() returned, not what was
// asked for. Sending maxSamples here would ship uninitialised buffer tail.
void test_short_read_is_forwarded_verbatim(void) {
  mic->returnSamples = 100;            // fewer than the 512 requested
  sandbox->startMicStream();
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, (int)sends.size());
  TEST_ASSERT_EQUAL_UINT(100 * sizeof(int16_t), sends[0]);
}

// "Nothing captured yet" is routine, not an error — it must not produce an
// empty frame on the wire.
void test_empty_read_sends_nothing(void) {
  mic->returnSamples = 0;
  sandbox->startMicStream();
  runLoop(3);
  TEST_ASSERT_EQUAL_INT(3, mic->calls);      // still polled every loop
  TEST_ASSERT_EQUAL_INT(0, (int)sends.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_streaming_sends_frames_and_stops);
  RUN_TEST(test_streaming_works_while_suspended);
  RUN_TEST(test_frame_size_is_clamped);
  RUN_TEST(test_pump_never_asks_the_driver_to_block);
  RUN_TEST(test_short_read_is_forwarded_verbatim);
  RUN_TEST(test_empty_read_sends_nothing);
  UNITY_END();
  return 0;
}
