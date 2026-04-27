// src/OutrunDeviceConfig.h
#ifndef OUTRUN_DEVICE_CONFIG_H
#define OUTRUN_DEVICE_CONFIG_H

#include "OutrunStatusLED.h"
#include "OutrunStatusDisplay.h"
#include "OutrunSandboxConfig.h"   // for ShaderTemplateFn, Extensions

namespace Outrun {

struct DeviceConfig {
  const char* deviceType = nullptr;
  const char* host = nullptr;
  StatusLED* statusLED = nullptr;
  StatusDisplay* statusDisplay = nullptr;
  ShaderTemplateFn shaderTemplate = nullptr;
  Extensions extensions;
};

} // namespace Outrun

#endif // OUTRUN_DEVICE_CONFIG_H
