// src/ResidentM5Mic.h
//
// SystemMic over M5Unified's built-in microphone. 16 kHz mono int16.
//
// OPT-IN: this header is not included by Resident.h. Include it from board
// code that targets an M5 device (M5StickS3, M5StickC Plus2, M5Stack ...):
//
//   #include <ResidentM5Mic.h>
//   Resident::M5Mic mic;
//   cfg.systemMic = &mic;
//
// M5Unified must be a dependency of YOUR project (it already is if you build
// for an M5 board); Resident itself does not depend on it. On a build without
// M5Unified this header compiles to nothing.
#ifndef RESIDENT_M5_MIC_H
#define RESIDENT_M5_MIC_H

#if __has_include(<M5Unified.h>)

#include <M5Unified.h>

#include <cstring>

#include "ResidentSystemMic.h"

namespace Resident {

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
//
// Slots are FRAME_SAMPLES wide and read() serves arbitrary caller sizes from
// an internal offset, so a 10 ms consumer can ride 32 ms slots. Slot size sets
// the re-queue deadline: the queue only stays non-empty while the caller comes
// back within one slot duration (32 ms at 512 samples — measured comfortable;
// 160 samples is not: a steady one underrun every three reads). Watch
// audit().underruns before shrinking anything.
//
// Contract notes (see ResidentSystemMic.h): capture runs begin()→end();
// timeoutMs == 0 is a non-blocking poll; short reads are legal; single caller.
class M5Mic : public SystemMic {
public:
  // Pipeline health counters, updated by read(). Both fault-ish counters read
  // 0 in a healthy steady state (a handful of underruns around boot/connect
  // is normal — the queue primes while the network stack hogs the CPU).
  struct Audit {
    uint32_t reads        = 0;
    uint32_t underruns    = 0;  // queue found empty: task parked, DMA chunk lost
    uint32_t tornSlots    = 0;  // sentinel survived: served an unfilled slot
    uint32_t timeouts     = 0;  // bounded wait (timeoutMs > 0) expired
    uint32_t maxWaitMs    = 0;  // longest wait for a free slot
    uint32_t maxReadGapMs = 0;  // widest interval between read() entries
  };

  bool begin() override {
    auto cfg = M5.Mic.config();
    cfg.sample_rate = SAMPLE_RATE;
    cfg.dma_buf_count = 4;
    cfg.dma_buf_len = 256;
    // M5Unified's mic task defaults to priority 2, unpinned; under load (TLS
    // streaming, inference) it runs late, the I2S DMA ring (~32 ms) overruns
    // and the audio tears. Core 0 carries everything that would preempt it —
    // WiFi at 23, esp_timer at 22, lwIP at 18, all pinned there by the
    // Arduino sdkconfig — while core 1 runs only app tasks, so pinned there
    // at 18 the mic task always wins promptly.
    cfg.task_priority = 18;
    cfg.task_pinned_core = 1;
    M5.Mic.config(cfg);
    _queued = 0;
    _serveOff = 0;
    return M5.Mic.begin();  // idempotent; releases the shared-codec speaker
  }

  // Stop the mic so a co-user (the speaker on a shared full-duplex codec) can
  // take the hardware. M5Unified's end() leaves any un-started request
  // queued, and the next begin() fills that stale buffer first — the rotation
  // absorbs it (one slot of wall time, never an unfilled serve), so no drain
  // is needed here.
  void end() override {
    _queued = 0;
    _serveOff = 0;
    M5.Mic.end();
  }

  int read(int16_t* buf, int maxSamples, int timeoutMs) override {
    if (!M5.Mic.isEnabled() || maxSamples <= 0) return 0;
    const unsigned long start = millis();
    if (_lastReadMs != 0) {
      uint32_t gap = (uint32_t)(start - _lastReadMs);
      if (gap > _audit.maxReadGapMs) _audit.maxReadGapMs = gap;
    }

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

    if (served > 0) {
      _audit.reads++;
      _lastReadMs = millis();
    }
    return served;  // a short read (0 included) is a legal answer
  }

  uint32_t sampleRate() const override { return SAMPLE_RATE; }
  int frameSamples() const override { return FRAME_SAMPLES; }

  const Audit& audit() const { return _audit; }
  // True when any fault counter is non-zero (cheap poll for a periodic dump).
  bool faulted() const {
    return _audit.underruns || _audit.tornSlots || _audit.timeouts;
  }
  // Zero the counters (e.g. at session start so a session-end dump reflects
  // one session, not boot-to-now). Benign race with read()'s increments —
  // worst case one event lands in the old epoch.
  void resetAudit() { _audit = Audit{}; }

private:
  static constexpr uint32_t SAMPLE_RATE = 16000;
  static constexpr int FRAME_SAMPLES = 512;
  // Requests M5Unified's queue holds, and therefore the rotation's lag. The
  // rotation needs one buffer more than that: two outstanding, one serving.
  static constexpr int QUEUE_DEPTH = 2;
  // Improbable PCM run stamped into a buffer before it is queued; a completed
  // fill overwrites it. Four in a row surviving = slot served unfilled.
  static constexpr int16_t kSentinel = 0x5A7A;

  // Hand the mic task its next destination, then pick up the buffer whose
  // fill that queueing waited on. False if no full buffer came free within
  // timeoutMs — immediately so when timeoutMs is 0 (non-blocking poll; that
  // is routine, not a fault, so it does not count as a timeout).
  bool advance(unsigned long start, int timeoutMs) {
    const bool priming = _queued < QUEUE_DEPTH;
    while (_queued < QUEUE_DEPTH) {  // priming: queue without serving
      if (!queueSlot()) return false;
      _queued++;
    }

    // Occupancy 0 outside priming means the mic task drained both requests
    // and parked, discarding its partial DMA chunk — the splice condition.
    // (Occupancy, not wait time: under a non-blocking caller, zero wait is
    // the healthy case, so HRD's waited-too-little heuristic can't be used.)
    if (!priming && M5.Mic.isRunning() && M5.Mic.isRecording() == 0) {
      _audit.underruns++;
    }

    // Wait for room here rather than inside record(), whose own spin is
    // untimed and would sail straight past timeoutMs. isRecording() is
    // occupancy only (2 = no room). It can under-report while the task is
    // still starting, which at worst sends us into record() a moment early —
    // harmless, because completion comes from record() returning, never from
    // this poll.
    const unsigned long t0 = millis();
    while (M5.Mic.isRecording() >= (size_t)QUEUE_DEPTH) {
      if (!M5.Mic.isRunning()) { _queued = 0; return false; }  // gone; re-prime
      if (timeoutMs <= 0) return false;      // non-blocking poll: nothing ready
      if ((long)(millis() - start) >= timeoutMs) {
        _audit.timeouts++;
        return false;
      }
      delay(1);
    }
    const uint32_t waited = (uint32_t)(millis() - t0);
    if (waited > _audit.maxWaitMs) _audit.maxWaitMs = waited;

    if (!queueSlot()) { _queued = 0; return false; }

    if (_buf[_slot][0] == kSentinel && _buf[_slot][1] == kSentinel &&
        _buf[_slot][2] == kSentinel && _buf[_slot][3] == kSentinel) {
      _audit.tornSlots++;
    }
    return true;
  }

  // Stamp the current slot and queue it, then rotate on to the slot whose
  // fill that queueing waited for — the one record() guarantees is complete.
  bool queueSlot() {
    _buf[_slot][0] = kSentinel; _buf[_slot][1] = kSentinel;
    _buf[_slot][2] = kSentinel; _buf[_slot][3] = kSentinel;
    if (!M5.Mic.record(_buf[_slot], FRAME_SAMPLES, SAMPLE_RATE)) return false;
    _slot = (_slot + 1) % (QUEUE_DEPTH + 1);
    return true;
  }

  int16_t _buf[QUEUE_DEPTH + 1][FRAME_SAMPLES];
  int _slot = 0;      // buffer being served, and the next one to queue
  int _serveOff = 0;  // samples of _buf[_slot] already handed out
  int _queued = 0;    // outstanding requests (< QUEUE_DEPTH while priming)
  Audit _audit;
  unsigned long _lastReadMs = 0;
};

} // namespace Resident

#endif // __has_include(<M5Unified.h>)
#endif // RESIDENT_M5_MIC_H
