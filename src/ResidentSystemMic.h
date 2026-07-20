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
  // Read up to maxSamples samples into buf; returns samples written (0 on
  // timeout / no data).
  virtual int read(int16_t* buf, int maxSamples, int timeoutMs) = 0;
  virtual uint32_t sampleRate() const = 0;   // e.g. 16000
  virtual int frameSamples() const = 0;      // natural read granularity
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_MIC_H
