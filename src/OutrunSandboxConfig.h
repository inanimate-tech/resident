// src/OutrunSandboxConfig.h
#ifndef OUTRUN_SANDBOX_CONFIG_H
#define OUTRUN_SANDBOX_CONFIG_H

#include <Arduino.h>
#include <map>
#include <functional>
#include "OutrunExtensions.h"

namespace Outrun {

using ShaderFields = std::map<String, String>;
using ShaderTemplateFn = String (*)(const ShaderFields& fields);
using TelemetryCallback = std::function<void(const char* json)>;

struct SandboxConfig {
  Extensions extensions;
  ShaderTemplateFn shaderTemplate = nullptr;
  TelemetryCallback telemetry;
  const char* timezone = nullptr;
};

} // namespace Outrun

#endif // OUTRUN_SANDBOX_CONFIG_H
