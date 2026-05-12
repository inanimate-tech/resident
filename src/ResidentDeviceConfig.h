// src/ResidentDeviceConfig.h
#ifndef RESIDENT_DEVICE_CONFIG_H
#define RESIDENT_DEVICE_CONFIG_H

#include "ResidentStatusLED.h"
#include "ResidentStatusDisplay.h"
#include "ResidentSandboxConfig.h"   // for ShaderTemplateFn, Extensions

namespace Resident {

struct DeviceConfig {
  const char* deviceType = nullptr;
  const char* host = nullptr;
  uint32_t dns1 = 0;
  uint32_t dns2 = 0;
  StatusLED* statusLED = nullptr;
  StatusDisplay* statusDisplay = nullptr;
  ShaderTemplateFn shaderTemplate = nullptr;
  Extensions extensions;
};

} // namespace Resident

#endif // RESIDENT_DEVICE_CONFIG_H
