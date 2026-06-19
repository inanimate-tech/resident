// src/ResidentSandboxConfig.h
#ifndef RESIDENT_SANDBOX_CONFIG_H
#define RESIDENT_SANDBOX_CONFIG_H

#include <Arduino.h>
#include <map>
#include <functional>
#include <optional>
#include <Courier.h>
#include "ResidentExtensions.h"
#include "ResidentStatusLED.h"
#include "ResidentStatusDisplay.h"
#include "ResidentSystemButton.h"
#include "ResidentPersistentStore.h"

namespace Resident {

using ShaderFields = std::map<String, String>;
using ShaderTemplateFn = String (*)(const ShaderFields& fields);
using TelemetryCallback = std::function<void(const char* json)>;

struct SandboxConfig {
  // Identifies the physical board (used for AP name and protocol path
  // defaults). Stays at top-level — it labels the device, not the network.
  const char* deviceType = nullptr;

  // Hardware bindings exposed to Lua, plus shader-expression template.
  Extensions extensions;
  ShaderTemplateFn shaderTemplate = nullptr;

  // Lua-side telemetry sink + per-board IANA timezone.
  TelemetryCallback telemetry = nullptr;
  const char* timezone = nullptr;

  // Status indicators are properties of the *device*, not the network —
  // top-level so a future standalone use case can drive them too. Resident's
  // internal handlers update them on connection state changes when network
  // is configured.
  StatusDisplay* statusDisplay = nullptr;
  StatusLED* statusLED = nullptr;

  // A button the runtime can poll directly (e.g. to skip the boot countdown).
  SystemButton* systemButton = nullptr;

  // App persistence. When persistApps is true (default), the last app that
  // loads successfully is saved and auto-reloaded on the next boot. Leave
  // persistentStore null to use the platform default (NVS on device); set it
  // to override the backing store (tests inject an in-memory fake).
  bool persistApps = true;
  PersistentStore* persistentStore = nullptr;

  // Networking opt-in. Presence ⇒ Sandbox constructs a Courier::Client with
  // this config, drives WiFi/transports, fires onConnected/onMessage/etc.
  // Absence ⇒ standalone runtime, no WiFi pulled in.
  std::optional<Courier::Config> network;
};

} // namespace Resident

#endif // RESIDENT_SANDBOX_CONFIG_H
