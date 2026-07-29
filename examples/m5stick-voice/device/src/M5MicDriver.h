#pragma once
#include <M5Unified.h>
#include <Resident.h>

#include <cstring>

// SystemMic backed by M5Unified's built-in microphone. 16 kHz mono int16.
//
// Reference implementation of Resident::SystemMic over an asynchronous capture
// backend — the contract it is written against is in src/ResidentSystemMic.h.
//
// M5.Mic.record() does not record. It queues a destination buffer into
// M5Unified's request queue and returns; the mic task fills the buffer in the
// background. Two consequences drive this whole design:
//
//  * The queue holds exactly two requests, and record() blocks until the slot
//    it wants to write is free. So record() returning tells you the request
//    queued two calls earlier has completed — a guarantee, with nothing to
//    poll. This driver rides it with a three-buffer rotation: queue the buffer
//    just finished with, and the buffer whose completion that queueing waited
//    on is the one to serve.
//
//    Do not use isRecording() as a completion signal. It is gated on a flag
//    the mic task sets itself, so a freshly queued request still reads back as
//    "nothing pending" and a wait on it falls straight through onto an
//    unfilled buffer. It answers occupancy — "is there room in the queue" —
//    which is all it is used for below.
//
//  * Queueing before serving keeps two requests outstanding at all times, so
//    the mic task always has somewhere to write. That is load-bearing: when
//    its queue runs empty the task parks, and on parking it throws away the
//    partially consumed DMA chunk it was holding. No error, no counter, just a
//    splice in the audio. A driver that queues a single buffer per read() runs
//    the queue dry — and takes that hit — on every single read.
class M5MicDriver : public Resident::SystemMic {
public:
  const char* name() const override { return "mic"; }

  void begin() override {
    auto cfg = M5.Mic.config();
    cfg.sample_rate = SAMPLE_RATE;
    // M5Unified's mic task defaults to priority 2, unpinned. This example
    // records while streaming over TLS, and under that load the task runs late
    // enough that the I2S DMA ring overruns and the audio tears. Core 0 already
    // carries everything that would preempt it — WiFi at 23, esp_timer at 22,
    // lwIP at 18, all pinned there by the Arduino sdkconfig — while core 1 runs
    // only app tasks, so pinned there at 18 the mic task always wins promptly.
    cfg.task_priority = 18;
    cfg.task_pinned_core = 1;
    M5.Mic.config(cfg);
    _queued = 0;
    _serveOff = 0;
    M5.Mic.begin();  // idempotent; releases shared I2S
  }

  int read(int16_t* buf, int maxSamples, int timeoutMs) override {
    if (!M5.Mic.isEnabled() || maxSamples <= 0) return 0;
    const unsigned long start = millis();

    int served = 0;
    while (served < maxSamples) {
      // Only rotate on a slot boundary — mid-slot there are samples in hand.
      if (_serveOff == 0 && !advance(start, timeoutMs)) break;
      int n = maxSamples - served;
      if (n > FRAME_SAMPLES - _serveOff) n = FRAME_SAMPLES - _serveOff;
      memcpy(buf + served, _buf[_slot] + _serveOff, n * sizeof(int16_t));
      served += n;
      _serveOff += n;
      if (_serveOff == FRAME_SAMPLES) _serveOff = 0;
    }
    return served;  // a short read (0 included) is a legal answer
  }

  uint32_t sampleRate() const override { return SAMPLE_RATE; }
  int frameSamples() const override { return FRAME_SAMPLES; }

private:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr int FRAME_SAMPLES = 512;
  // Requests M5Unified's queue holds, and therefore the rotation's lag. The
  // rotation needs one buffer more than that: two outstanding, one being
  // served. Slot size sets the deadline for re-queueing — the queue only stays
  // non-empty while read() comes back around within one slot duration, and at
  // 512 samples (32 ms) the pump clears that comfortably.
  static constexpr int QUEUE_DEPTH = 2;

  // Hand the mic task its next destination, then pick up the buffer whose fill
  // that queueing waited on. False if no full buffer came free within
  // timeoutMs — immediately so when timeoutMs is 0.
  bool advance(unsigned long start, int timeoutMs) {
    while (_queued < QUEUE_DEPTH) {  // priming: queue without serving
      if (!queueSlot()) return false;
      _queued++;
    }
    // Wait for room here rather than inside record(), whose own spin is untimed
    // and would sail straight past timeoutMs. isRecording() is occupancy only
    // (2 = no room). It can under-report while the task is still starting,
    // which at worst sends us into record() a moment early — harmless, because
    // completion comes from record() returning, never from this poll.
    while (M5.Mic.isRecording() >= (size_t)QUEUE_DEPTH) {
      if (!M5.Mic.isRunning()) { _queued = 0; return false; }  // gone; re-prime
      if (timeoutMs <= 0 || (long)(millis() - start) >= timeoutMs) return false;
      delay(1);
    }
    if (!queueSlot()) { _queued = 0; return false; }
    return true;
  }

  // Queue the current slot, then rotate on to the slot whose fill that
  // queueing waited for — the one record() guarantees is complete.
  bool queueSlot() {
    if (!M5.Mic.record(_buf[_slot], FRAME_SAMPLES, SAMPLE_RATE)) return false;
    _slot = (_slot + 1) % (QUEUE_DEPTH + 1);
    return true;
  }

  int16_t _buf[QUEUE_DEPTH + 1][FRAME_SAMPLES];
  int _slot = 0;      // buffer being served, and the next one to queue
  int _serveOff = 0;  // samples of _buf[_slot] already handed out
  int _queued = 0;    // outstanding requests (< QUEUE_DEPTH while priming)
};
