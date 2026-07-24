#pragma once
#include <M5Unified.h>
#include <Resident.h>

#include <cstring>

// SystemMic backed by M5Unified's built-in microphone. 16 kHz mono int16.
class M5MicDriver : public Resident::SystemMic {
public:
  const char* name() const override { return "mic"; }
  void begin() override { M5.Mic.begin(); }  // idempotent; releases shared I2S
  // M5.Mic.record() is asynchronous: it queues the destination buffer and
  // returns while M5Unified's mic task fills it in the background. So the
  // destination must be a buffer we own — handing over the caller's would let
  // the task keep writing it after read() returns — and the copy out must wait
  // for the fill to complete.
  int read(int16_t* buf, int maxSamples, int /*timeoutMs*/) override {
    if (!M5.Mic.isEnabled()) return 0;
    const int n = maxSamples < FRAME_SAMPLES ? maxSamples : FRAME_SAMPLES;
    if (!M5.Mic.record(_staging, n, SAMPLE_RATE)) return 0;
    // isRecording() is gated on a flag the mic task sets itself, so it reads 0
    // until the task dequeues this request. Give it a tick to start before
    // polling, or the wait falls through and we copy an unfilled buffer.
    delay(1);
    while (M5.Mic.isRecording()) { delay(1); }
    memcpy(buf, _staging, n * sizeof(int16_t));
    return n;
  }
  uint32_t sampleRate() const override { return SAMPLE_RATE; }
  int frameSamples() const override { return FRAME_SAMPLES; }
private:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr int FRAME_SAMPLES = 512;
  int16_t _staging[FRAME_SAMPLES];
};
