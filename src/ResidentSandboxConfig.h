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
// Feature test for SandboxConfig::executionDeadlineMs and
// executionSoftDeadlineMs, so a board that also builds against an older
// published SDK can guard its use of them on the fields' presence.
#define RESIDENT_HAS_EXECUTION_DEADLINE 1

struct SandboxConfig {
  // Identifies the physical board (used for AP name and protocol path
  // defaults). Stays at top-level — it labels the device, not the network.
  const char* deviceType = nullptr;

  // Optional self-description carried in the device hello (see docs/api.md
  // "Hello"). firmwareVersion is the BOARD build's version string (not
  // Resident's); profileRef names the out-of-band authoring document for
  // this device type, with its version (e.g. "m5stick@2"). Both omitted
  // from the hello when null.
  const char* firmwareVersion = nullptr;
  const char* profileRef = nullptr;

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

  // Offline-first (0.8): ticking and event dispatch no longer gate on
  // connectivity — a disconnected device keeps running its app; only
  // network sends wait. Set true to restore the old gated behavior.
  bool gateTickOnConnection = false;

  // Sandbox hardening (0.8): by default the app environment does NOT get
  // os / io / package / require / load / dofile / loadstring / loadfile /
  // debug. Set true to open the full standard library (trusted builds
  // whose apps predate the closed default).
  bool openUnsafeLibs = false;

  // Fresh app environment (0.8): each loadApp resets the globals to the
  // runtime baseline — nothing from the previous app survives except the
  // store slot. Set false for builds whose apps relied on cross-load
  // global leakage.
  bool freshAppEnvironment = true;

  // Per-dispatch WALL-CLOCK hard deadline, in milliseconds (0.8): init, one
  // tick, one event, one chunk, or one framework hook may run for at most this
  // long before the dispatch is aborted with a runtime_error — the app
  // survives. Raise it for a board whose apps legitimately run longer.
  // Available on every platform; 0 = no hard guard.
  uint32_t executionDeadlineMs = 1000;

  // Per-dispatch WALL-CLOCK soft deadline, in milliseconds. A dispatch that
  // outlives it is reported — a rate-limited serial line plus a
  // `slow_dispatch` telemetry event — and allowed to finish. Independent of
  // executionDeadlineMs and of the platform; 0 = no reporting.
  uint32_t executionSoftDeadlineMs = 0;

  // Framework module (0.8): privileged runtime code the sandbox hosts
  // OUTSIDE the app — its own environment, the runtime-channel sender,
  // lifecycle interception, and a persistent update slot written only by
  // {channel:"system", type:"framework"} messages. Resident is generic
  // here: `name` is whatever framework the board embeds; the sandbox
  // never interprets it.
  struct FrameworkConfig {
    const char* name = nullptr;     // announced in the hello
    int version = 0;                // built-in copy's version
    const char* source = nullptr;   // built-in Lua source (the cold-start default)
  };
  std::optional<FrameworkConfig> framework;

  // Networking opt-in. Presence ⇒ Sandbox constructs a Courier::Client with
  // this config, drives WiFi/transports, fires onConnected/onMessage/etc.
  // Absence ⇒ standalone runtime, no WiFi pulled in.
  std::optional<Courier::Config> network;
};
#pragma GCC diagnostic pop

} // namespace Resident

#endif // RESIDENT_SANDBOX_CONFIG_H
