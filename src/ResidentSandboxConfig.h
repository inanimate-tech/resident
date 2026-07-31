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
#include "ResidentSystemLED.h"
#include "ResidentSystemDisplay.h"
#include "ResidentSystemButton.h"
#include "ResidentSystemMic.h"
#include "ResidentPersistentStore.h"

namespace Resident {

using ShaderFields = std::map<String, String>;
using ShaderTemplateFn = String (*)(const ShaderFields& fields);
using TelemetryCallback = std::function<void(const char* json)>;

// The struct as a whole must be wrapped (not just the deprecated fields
// below): Sandbox::configure() copies SandboxConfig by value, and the
// compiler-generated copy constructor/assignment operator for a struct
// containing [[deprecated]] members is itself implicitly deprecated. That
// makes -Wdeprecated-declarations fire on every `Sandbox(cfg)` construction,
// even ones that never touch statusDisplay/statusLED. Suppressing only the
// field declarations does not prevent this; the whole struct definition
// must be in the pragma region so the implicit special members are
// synthesized without triggering the warning.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
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

  // System-managed indicators (role slots) are properties of the *device*,
  // not the network — top-level so a future standalone use case can drive
  // them too. Resident's internal handlers update them on connection state
  // changes when network is configured. Renamed from statusDisplay/
  // statusLED; the old names remain as deprecated aliases below.
  SystemDisplay* systemDisplay = nullptr;
  SystemLED* systemLED = nullptr;

  // A button the runtime can poll directly (e.g. to skip the boot countdown).
  SystemButton* systemButton = nullptr;

  // A capture source for the mic streaming pump. Not a Driver: the pump owns
  // its begin()/end(), so capture runs only while streaming. On M5 boards use
  // the shipped Resident::M5Mic (#include <ResidentM5Mic.h>).
  SystemMic* systemMic = nullptr;

  // Deprecated: use systemDisplay / systemLED. Reconciled internally — the
  // new field wins; these are the fallback.
  [[deprecated("use systemDisplay")]] SystemDisplay* statusDisplay = nullptr;
  [[deprecated("use systemLED")]] SystemLED* statusLED = nullptr;

  // App persistence. When persistApps is true (default), the last app that
  // loads successfully is saved and auto-reloaded on the next boot. Leave
  // persistentStore null to use the platform default (NVS on device); set it
  // to override the backing store (tests inject an in-memory fake).
  bool persistApps = true;
  PersistentStore* persistentStore = nullptr;

  // Wall-clock budget for one continuous slice of on_tick, in milliseconds.
  // A frame that outruns it is yielded mid-flight: Sandbox::loop() returns so
  // the rest of the system (Courier, driver update(), overlays, the system
  // button) gets its turn, and the SAME frame resumes on the next pass. An
  // expensive app therefore degrades to a lower frame rate instead of holding
  // the main loop for the whole frame.
  //
  // A frame that finishes inside its slice is untouched — same single call,
  // same timing as before. ctx is built once per frame, so time_ms and the
  // time-of-day fields do not move between slices and motion computed from
  // them stays correct.
  //
  // 0 disables slicing: on_tick runs as one uninterrupted call.
  //
  // Slicing cannot interrupt a frame that is below a C-call boundary (an
  // extension binding that called back into Lua); such a frame runs to
  // completion in the current pass.
  uint32_t tickSliceMs = 8;

  // Networking opt-in. Presence ⇒ Sandbox constructs a Courier::Client with
  // this config, drives WiFi/transports, fires onConnected/onMessage/etc.
  // Absence ⇒ standalone runtime, no WiFi pulled in.
  std::optional<Courier::Config> network;
};
#pragma GCC diagnostic pop

} // namespace Resident

#endif // RESIDENT_SANDBOX_CONFIG_H
