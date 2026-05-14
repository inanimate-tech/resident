# Resident::Sandbox rename + callback API

Date: 2026-05-14
Status: Draft

## Goal

Bring Resident's public C++ surface into alignment with (a) the landing-page
story ("Sandbox runtime with hot reload for ESP32 devices") and (b) Courier's
extension idiom (instantiate + register callbacks, do not subclass).

The end state: a developer reading the landing page and a developer reading
`main.cpp` see the same primary noun (`Sandbox`); a developer crossing the
Resident/Courier boundary uses one mental model in both libraries.

Pre-1.0; breaking changes are acceptable in this revision.

## Problems being solved

1. **Story/code mismatch.** The landing page leads with "Sandbox"; the golden
   path code leads with `Resident::Device`. The dev never instantiates a
   `Sandbox` directly in normal use; it's hidden behind `device.sandbox()`.
2. **`Resident::Device` overclaims scope.** It's a *component installed on*
   the board (sits next to `pinMode()` and other peripheral init in `setup()`),
   but the name suggests it owns the whole device.
3. **Pattern inconsistency with Courier.** Resident exposes extension points
   as virtual methods to override; Courier exposes them as callbacks to
   register. Devs have to switch mental models when crossing the boundary.
4. **Pass-through bloat in `DeviceConfig`.** Every Courier knob the dev wants
   has to be re-exported by Resident. `dns1`/`dns2` were added recently for
   this reason; TLS cert support would be next. Resident shouldn't be in the
   business of mirroring Courier's config surface.
5. **"Device" overload.** The word means three different things today:
   physical board, the C++ class, and the `cfg.deviceType` label. Two of
   those are clean (physical board, label); the C++ class is the offender.

## Non-goals

- No changes to the Lua runtime semantics (10 FPS `on_tick`, `ctx` shape,
  message protocol).
- No changes to the wire protocol (`/devices/<id>` path, `app`/`shader`/
  `app_event` JSON shapes, device IDs).
- No changes to physical-board vocabulary (`device/` folder, `DEVICE-SKILL.md`,
  `--device-id`, `cfg.deviceType`, `/resident:write-device-skill` skill).
- No changes to `Resident::Driver` / `Resident::Extension` / `Resident::LuaModule`.
  Driver authoring is unchanged.
- No changes to the agent skills (`/resident:create-app`, `/resident:push-app`,
  `/resident:validate-app`, `/resident:write-device-skill`).

## Vocabulary mapping

| Concept | Today | After |
|---|---|---|
| Lua runtime + composed network host | `Resident::Sandbox` (standalone) + `Resident::Device` (connected) | **`Resident::Sandbox`** (one class; networking is config) |
| Config struct | `SandboxConfig` + `DeviceConfig` | **`Resident::SandboxConfig`** (one struct; `cfg.network` opt-in) |
| Header | `ResidentSandbox.h` + `ResidentDevice.h` | **`Resident.h`** umbrella + `ResidentSandbox.h` |
| Subclass-and-override pattern | `class FeatherDevice : public Resident::Device { void onConnected() override; }` | **`sandbox.onConnected(cb)`** — register, don't subclass |
| Status indicators | nested under `DeviceConfig` | top-level on `SandboxConfig` |
| Network host/port/dns/etc. | flat fields on `DeviceConfig` (re-exported from Courier) | `cfg.network` takes `Courier::Config` directly |
| Hardware bindings | `Resident::Driver`, `Resident::Extension` | unchanged |
| Physical board (folder, doc, ID, protocol path, type label) | `device/`, `DEVICE-SKILL.md`, `--device-id`, `/devices/<id>`, `cfg.deviceType` | unchanged — these consistently mean "the addressable physical thing" and stay |

## API surface (after)

### `Resident::SandboxConfig`

```cpp
namespace Resident {

struct SandboxConfig {
  const char*       deviceType     = nullptr;   // labels physical board (kept)
  Extensions        extensions;
  ShaderTemplateFn  shaderTemplate = nullptr;
  TelemetryCallback telemetry      = nullptr;
  const char*       timezone       = nullptr;
  StatusDisplay*    statusDisplay  = nullptr;
  StatusLED*        statusLED      = nullptr;
  std::optional<Courier::Config> network;       // omit ⇒ no WiFi pulled in
};

}
```

Six fields plus an optional `network`. The presence of `cfg.network` is the
*only* signal that this Sandbox is connected. New Courier knobs (cert, dns,
custom transports) do not require a Resident change — they live inside
`Courier::Config` or are configured via the `onConfigureNetwork` callback.

### `Resident::Sandbox`

```cpp
namespace Resident {

class Sandbox {
 public:
  explicit Sandbox(const SandboxConfig& cfg);
  ~Sandbox();

  void setup();
  void loop();

  // ── Setup-phase callback (register before setup()) ──
  using ConfigureNetworkCallback = std::function<void(Courier::Client&)>;
  void onConfigureNetwork(ConfigureNetworkCallback cb);

  // ── Reactive callbacks (single-slot, last registration wins) ──
  using TransportsWillConnectCallback = std::function<void()>;
  using MessageCallback = std::function<void(const char* transportName,
                                              const char* type,
                                              JsonDocument& doc)>;
  using ConnectionChangeCallback = std::function<void(Courier::State)>;
  using ConnectedCallback = std::function<void()>;

  void onTransportsWillConnect(TransportsWillConnectCallback cb);
  void onMessage(MessageCallback cb);                 // fires for non-reserved types only
  void onConnectionChange(ConnectionChangeCallback cb);
  void onConnected(ConnectedCallback cb);

  // ── Sandbox controls ──
  void loadApp(const char* code);
  void loadShader(const ShaderFields& fields);
  void sendAppEvent(const char* name, const char* dataJson);
  void setTimezone(const char* iana);
  bool hasTimezone() const;
  bool isAppRunning() const;

  // ── State + accessors ──
  String  getDeviceId() const;
  const char* getDeviceType() const;
  bool    isConnected() const;
  bool    isTimeSynced() const;
  Courier::Client&             courier();              // present iff cfg.network was set
  Courier::WebSocketTransport& ws();                   // present iff cfg.network was set
};

}
```

Single-slot callbacks (last registration wins) match Courier's convention.
Internal handlers driving `statusDisplay`/`statusLED` from connection state
are *not* on the public callback machinery — they fire alongside, so the
user's `onConnectionChange` callback adds to (does not replace) the default
status-indicator behavior. Setting `statusDisplay = nullptr` opts out of the
default behavior.

`courier()` and `ws()` are valid only when `cfg.network` was set; otherwise
asserting on access. Document this clearly.

### Lifecycle ordering

```
Sandbox::Sandbox(cfg)
  if (cfg.network) construct Courier internally with cfg.network value
  internal Courier hooks wired (Resident's own onMessage routes app/shader/
  event; status-indicator handlers wire to onConnectionChange)

Sandbox::setup()
  fire onConfigureNetwork(courier)              ← register transports, certs,
                                                  custom WiFi callback, etc.
                                                  (only fires if cfg.network)
  apply AP name, status display begin(), etc.
  sandbox.initialize()                          ← Lua state up, extensions registered
  if (cfg.network) courier.setup()              ← WiFi + transports begin
    inside: fires onTransportsWillConnect
    inside: connection state machine drives statusDisplay/statusLED
    inside: when connected, fires onConnected

Sandbox::loop()
  if (cfg.network) courier.loop()
  statusDisplay->update() if any
  if (isConnected() || standalone) sandbox tick (10 FPS on_tick)
```

`onConfigureNetwork` is the *only* place that fires before transports begin;
all other reactive callbacks fire later, driven by Courier's own state
machine. The callback name describes the role; the argument it receives
(`Courier::Client&`) names the implementation honestly.

## Golden path (after)

### Connected (the common case)

```cpp
#include <Resident.h>
#include "DisplayDriver.h"
#include "LEDDriver.h"
#include "BatteryDriver.h"

DisplayDriver display{...};
LEDDriver     led{...};
BatteryDriver battery{...};

Resident::SandboxConfig makeConfig() {
  Resident::SandboxConfig cfg;
  cfg.deviceType    = "feather-tft";
  cfg.extensions    = {&display, &led, &battery};
  cfg.statusDisplay = &display;
  cfg.statusLED     = &led;
  cfg.network       = Courier::Config{ .host = "resident.inanimate.tech" };
  return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
  // Override the default WS path to the canonical /devices/<id> route.
  sandbox.onTransportsWillConnect([]() {
    String wsPath = String("/devices/") + sandbox.getDeviceId();
    sandbox.ws().setEndpoint("resident.inanimate.tech", 443, wsPath.c_str());
  });

  // First connect → load a one-shot bootstrap app showing the device ID.
  sandbox.onConnected([]() {
    String app = "function init(ctx) screen.text(...) end";
    sandbox.loadApp(app.c_str());
  });

  sandbox.setup();
}

void loop() { sandbox.loop(); }
```

No `class FeatherDevice`. No subclassing. No `device.sandbox()` indirection.
The dev's `main.cpp` deals with one Resident type and a handful of
registrations.

### Standalone (no network)

```cpp
#include <Resident.h>
#include "MyLEDDriver.h"

MyLEDDriver led;

Resident::SandboxConfig makeConfig() {
  Resident::SandboxConfig cfg;
  cfg.extensions = {&led};
  // no cfg.network ⇒ no WiFi/Courier pulled in
  return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
  sandbox.setup();
  sandbox.loadApp(
    "function on_tick(ctx, dt_ms)\n"
    "  led.set_rgb(math.sin(ctx.time_ms/1000)*127+128, 0, 0)\n"
    "end\n"
  );
}

void loop() { sandbox.loop(); }
```

### Diving into the network layer (TLS cert, custom headers, additional transports)

```cpp
sandbox.onConfigureNetwork([](Courier::Client& c) {
  c.transport<Courier::WebSocketTransport>("ws").onConfigure(
    [](esp_websocket_client_config_t& ws) { ws.cert_pem = MY_CERT; }
  );
  // could also: c.addTransport<MqttTransport>("mqtt", ...);
  // could also: c.onConfigureWiFi([](WiFiManager& wm) { ... });
});
```

The callback receives `Courier::Client&` directly — no Resident wrapper, no
abstraction layer. Resident never has to grow a passthrough field for any
new Courier feature.

## Migration

### Internal: library code

- Delete `src/ResidentDevice.h`, `src/ResidentDevice.cpp`, `src/ResidentDeviceConfig.h`.
- Move the *behavior* of `Device::setup()` / `Device::loop()` / connection-
  state handling into `Sandbox`, gated on `cfg.network.has_value()`.
- Convert each former virtual hook into a `std::function` member with a
  setter (`onMessage(cb)`, etc.).
- Carve out Resident's internal routing of `app`/`shader`/`app_event` so it's
  *not* user-overridable; user `onMessage(cb)` only fires for unknown types.
- Update `src/Resident.h` umbrella header to re-export the relevant pieces
  (no longer includes `ResidentDevice.h`).
- Update `src/ResidentSandbox.h` / `.cpp` for the expanded surface.
- Update `CMakeLists.txt` `idf_component_register` SRCS list (drop
  `src/ResidentDevice.cpp`).
- Bump `library.json` version to `0.5.0-dev` during the change; release as
  `0.5.0`.

### Internal: examples

Every example that uses Resident must be updated. The `class FooDevice :
public Resident::Device` boilerplate disappears in each; tiny override
bodies become callback registrations in `setup()`.

| Example | File(s) | Notes |
|---|---|---|
| m5stick-demo | `examples/m5stick-demo/device/src/main.cpp` | Currently in CI. |
| Feather (full) | `examples/adafruit-esp32-s2-feather/device/src/main.cpp` | **Add to CI** (see below). |
| Feather (minimal Resident) | `examples/adafruit-esp32-s2-feather/device-minimal-resident/src/main.cpp` | **Add to CI**. |
| Feather (no Resident) | `examples/adafruit-esp32-s2-feather/device-no-resident/` | No Resident usage; not affected. |
| ESP-IDF basic | `examples/espidf-basic/main/main.cpp` | Currently in CI (espidf job). |

### Internal: tests

- `test/unit/test/test_extensions/test_extensions.cpp` — references the old
  surface; update to the new one. Verify all unit tests pass on native after
  the rename.

### Internal: documentation

Every doc that references the C++ surface must be updated:

| File | Update |
|---|---|
| `README.md` | Collapse the two-example structure into one, with a sidebar for the standalone case. Use `Resident::Sandbox` throughout. |
| `docs/start-building.md` | Step 2 ("Add Resident") rewritten around `Resident::Sandbox`. Drop subclass boilerplate in code samples. |
| `docs/api.md` | Full API reference rewrite for the new surface. |
| `docs/changelog.md` | Add 0.5.0 entry summarising the breaking changes and migration. |
| `examples/adafruit-esp32-s2-feather/device-minimal-resident/README.md` | Update code examples and prose. |
| `examples/adafruit-esp32-s2-feather/device/README.md` | Update code examples and prose. |
| `tools/agent-plugin/skills/write-device-skill/SKILL.md` | If it references the C++ surface (likely does in walkthrough sections), update. |

Additionally, **grep for residual references** across the whole repo before
declaring the rename done:

```sh
git grep -nE 'Resident::Device\b|ResidentDevice\.h|DeviceConfig\b' \
  -- ':!docs/changelog.md' \
     ':!docs/superpowers/specs/' \
     ':!examples/*/docs/'
```

Excluded paths are historical records (changelog, prior specs) and per-
example archived docs whose accuracy reflects the API at the time they were
written.

### Internal: landing page / website

`https://resident.inanimate.tech/` is in a separate repo (not this one), but
the rename touches its hero example. Coordinate a follow-up PR there to land
alongside the 0.5.0 release.

### CI

The build matrix today:

- `m5stick-demo/device/` (PlatformIO) ✓
- `espidf-basic/` (ESP-IDF) ✓

Missing: both Feather PlatformIO projects. **Expand `tools/run-tests.py
build` to iterate over all Resident-using PlatformIO projects** rather than
hard-coding the m5stick path. Then the workflow's `build-platformio` job
exercises the rename across every example. Concretely:

```python
# tools/run-tests.py — current
project = ROOT / "examples" / "m5stick-demo" / "device"
if not run_cmd(["pio", "run"], cwd=project, ...): ...

# after
PROJECTS = [
    ROOT / "examples" / "m5stick-demo" / "device",
    ROOT / "examples" / "adafruit-esp32-s2-feather" / "device",
    ROOT / "examples" / "adafruit-esp32-s2-feather" / "device-minimal-resident",
]
for project in PROJECTS:
    if not run_cmd(["pio", "run"], cwd=project, ...): ...
```

The Feather projects depend on Adafruit libraries (LC709203F, ST7789,
NeoPixel, GFX) — all on the PlatformIO registry, so they should resolve
without additional CI setup. Confirm during implementation.

The `device-no-resident/` Feather project doesn't use Resident; either skip
it from the Resident-CI matrix, or include it as a smoke test that the
hardware-bringup baseline still builds. Include it for completeness.

### External (downstream firmware projects, e.g. hawthorn-firmware)

Pre-1.0 means breaking changes are acceptable, but a brief migration note is
warranted:

```
// Before
class MyDevice : public Resident::Device {
  void onConnected() override { ... }
};
MyDevice device;
device.setup(); device.loop();

// After
Resident::Sandbox sandbox{cfg};
sandbox.onConnected([]() { ... });
sandbox.setup(); sandbox.loop();
```

Mechanical transformation in most cases. The only non-trivial one is callers
that did `Device::onMessage(...)` super-calls to keep sandbox routing — those
become unnecessary (Resident routes its reserved types internally; user
callback only fires for non-reserved).

### Version bump

This is a 0.5.0 release. The 0.4.x line is the last with `Resident::Device`.

## Open implementation questions

These are decisions for the implementation plan, not the design:

1. **`std::optional<Courier::Config>` vs pointer vs flag.** PlatformIO's
   espressif32@6.12 generally compiles arduino-esp32 with `-std=gnu++17`,
   making `std::optional` available, but custom build configurations may
   strip it. Fall back: `Courier::Config network; bool useNetwork = false;`
   (less elegant but C++11-safe).
2. **Where Resident's internal status-indicator wiring lives.** Today it's
   inline in `Device::onConnectionChange`. Under the callback model, it
   should be a private internal handler that runs unconditionally if
   `statusDisplay`/`statusLED` are non-null, so the user's optional
   `onConnectionChange` callback adds to (does not replace) it. Confirm
   the wiring in implementation.
3. **Asserting on `courier()`/`ws()` in standalone mode.** Document the
   precondition; consider returning a guaranteed-non-null reference in
   networked mode and asserting in standalone, vs. returning a pointer that
   may be null. Reference reads cleaner; pointer is more honest about
   nullability.

## What's not in scope

- Renaming `cfg.extensions` → `cfg.modules`. (Considered; deferred — a
  separate small ask if desired.)
- Changing `DEVICE-SKILL.md` filename or `device/` folder convention. The
  word "device" stays as the consistent term for the physical board.
- Changing the protocol path `/devices/<id>` or any wire format.
- Changing `Resident::Driver` lifecycle (begin/update/registerModule/etc.).

## Story update (landing page + README)

After this change, the headline noun and the primary class agree:

- Landing page tagline stays *"Sandbox runtime with hot reload for ESP32
  devices"* — but the very next code block uses `Resident::Sandbox`
  directly, so the headline noun = the type the dev sees in code.
- README's "Standalone" / "Network-connected" sections collapse into one
  example with a sidebar: *"omit `cfg.network` to run without WiFi."*
- `start-building.md` step 2 reads *"Add the Resident sandbox"* and the
  example code matches.

That's the whole DX win in one sentence: **the sandbox the landing page
talks about is the same sandbox the developer instantiates.**
