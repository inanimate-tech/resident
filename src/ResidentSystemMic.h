// src/ResidentSystemMic.h
#ifndef RESIDENT_SYSTEM_MIC_H
#define RESIDENT_SYSTEM_MIC_H

#include <cstdint>
#include "ResidentDriver.h"

namespace Resident {

// A system-managed microphone, assigned via SandboxConfig::systemMic. Captures
// 16-bit signed mono PCM. Hardware init runs in the Driver lifecycle (begin()).
class SystemMic : public Driver {
public:
  // Drain already-captured audio into buf; returns the number of samples
  // written (0 if none are ready).
  //
  // Implementor contract. Capture backends are typically asynchronous — a DMA
  // or task-driven pipeline filling buffers in the background — and every
  // point below has silently corrupted audio in a real driver:
  //
  //  1. CAPTURE IS begin()'s JOB, NOT read()'s. Keep the pipeline running and
  //     its buffers queued from begin() onwards; read() only copies out what
  //     has already landed. Do not start a capture inside read() and wait for
  //     that one buffer. A pipeline holding a destination only while read() is
  //     in flight loses everything in between — backends commonly discard the
  //     partial chunk they were holding whenever their queue runs dry, with no
  //     error and no counter, and each gap is an audible splice.
  //
  //  2. NEVER GIVE CAPTURE HARDWARE THE CALLER'S BUFFER. Record into memory
  //     the driver owns and copy out of it. An async backend goes on writing
  //     its destination after the call that queued it has returned, so handing
  //     it `buf` is a background write into memory the caller already
  //     considers its own.
  //
  //  3. timeoutMs == 0 MEANS DO NOT BLOCK — return what is ready this instant,
  //     0 if that is nothing. The runtime's mic pump calls
  //     read(buf, frameSamples(), 0) from Sandbox::loop(), so blocking here
  //     stalls the whole sandbox: app ticks, transports, overlays and all.
  //     timeoutMs > 0 is a ceiling to respect, not a duration to fill. Never
  //     block unbounded, whatever the timeout.
  //
  //  4. SHORT READS ARE LEGAL. The return value is authoritative — never
  //     assume maxSamples were written. Returning 0 is routine and simply
  //     means nothing has been captured yet.
  //
  // examples/m5stick-voice/device/src/M5MicDriver.h is a worked reference over
  // an asynchronous backend.
  virtual int read(int16_t* buf, int maxSamples, int timeoutMs) = 0;
  virtual uint32_t sampleRate() const = 0;   // e.g. 16000
  virtual int frameSamples() const = 0;      // natural read granularity
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_MIC_H
