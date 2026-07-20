#pragma once
#include <M5Unified.h>
#include <Resident.h>

// SystemMic backed by M5Unified's built-in microphone. 16 kHz mono int16.
class M5MicDriver : public Resident::SystemMic {
public:
  const char* name() const override { return "mic"; }
  void begin() override { M5.Mic.begin(); }  // idempotent; releases shared I2S
  int read(int16_t* buf, int maxSamples, int /*timeoutMs*/) override {
    if (!M5.Mic.isEnabled()) return 0;
    if (M5.Mic.record(buf, maxSamples, SAMPLE_RATE)) return maxSamples;
    return 0;
  }
  uint32_t sampleRate() const override { return SAMPLE_RATE; }
  int frameSamples() const override { return FRAME_SAMPLES; }
private:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr int FRAME_SAMPLES = 512;
};
