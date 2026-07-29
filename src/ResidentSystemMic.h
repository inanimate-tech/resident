// src/ResidentSystemMic.h
#ifndef RESIDENT_SYSTEM_MIC_H
#define RESIDENT_SYSTEM_MIC_H

#include <cstdint>

namespace Resident {

// A capture source, assigned via SandboxConfig::systemMic. 16-bit signed mono
// PCM; sampleRate() is the single source of truth for the wire format.
//
// Deliberately NOT a Driver: it has no Lua surface, no event sink, and no
// per-loop update. Capture runs between begin() and end(), owned by whoever
// streams — the runtime's mic pump calls begin() in startMicStream() and
// end() in stopMicStream(), so the hardware (a shared codec, a capture task)
// is held only while streaming. Any Lua-facing mic activity (levels, meters)
// is a separate extension's concern, fed by the board's own plumbing.
//
// Implementor contract. Capture backends are typically asynchronous — a DMA
// or task-driven pipeline filling buffers in the background — and every rule
// below has silently corrupted audio in a real driver:
//
//  1. CAPTURE RUNS FROM begin() TO end(); read() ONLY DRAINS. begin()
//     acquires the hardware, starts the pipeline, and keeps its buffers
//     queued; it returns false if the hardware could not be acquired, and
//     calling it again while capturing must be benign. end() stops capture
//     and releases the hardware for a co-user of a shared codec. Do not
//     start a capture inside read() and wait for that one buffer — a
//     pipeline holding a destination only while read() is in flight loses
//     everything in between: backends commonly discard the partial chunk
//     they were holding whenever their queue runs dry, with no error and no
//     counter, and each gap is an audible splice.
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
//  5. SINGLE CALLER. read() is called by exactly one reader — the runtime's
//     mic pump, or one producer task. Drivers need no internal locking
//     against concurrent reads, and callers must not introduce them.
//
// src/ResidentM5Mic.h is a worked reference over an asynchronous backend
// (M5Unified), including how it derives buffer completion without polling.
class SystemMic {
public:
  virtual ~SystemMic() = default;

  // Acquire the capture hardware and start the pipeline. False = failed.
  virtual bool begin() = 0;

  // Stop capture and release the hardware (e.g. for the speaker side of a
  // shared full-duplex codec). Default no-op for mics that share nothing.
  virtual void end() {}

  // Drain already-captured audio into buf; returns the number of samples
  // written (0 if none are ready). See the contract above.
  virtual int read(int16_t* buf, int maxSamples, int timeoutMs) = 0;

  virtual uint32_t sampleRate() const = 0;         // e.g. 16000
  virtual int frameSamples() const { return 512; } // natural read granularity
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_MIC_H
