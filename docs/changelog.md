# Changelog

## v0.5.0

Theme: collapse `Resident::Device` into `Resident::Sandbox`; replace virtual
subclass hooks with callback registration to align with Courier's idiom.

### Breaking changes

**`Resident::Device` is gone.** The C++ surface is now a single
`Resident::Sandbox` class. Examples that previously subclassed Device need
to migrate to callback registration:

```cpp
// Before
class FeatherDevice : public Resident::Device {
  void onConnected() override { ... }
};
FeatherDevice device;
device.setup(); device.loop();

// After
Resident::Sandbox sandbox{cfg};
sandbox.onConnected([]() { ... });
sandbox.setup(); sandbox.loop();
```

**`Resident::DeviceConfig` is gone.** Its fields (`deviceType`,
`statusDisplay`, `statusLED`, `host`/`port`/`dns1`/`dns2`) are now on
`Resident::SandboxConfig`. Network-related fields move into
`cfg.network`, which is a `std::optional<Courier::Config>` — set it to
enable WiFi/transports; omit for standalone-runtime mode:

```cpp
Resident::SandboxConfig cfg;
cfg.deviceType    = "feather-tft";
cfg.extensions    = {&display, &led, &battery};
cfg.statusDisplay = &display;
cfg.statusLED     = &led;

// Use direct field assignment on Courier::Config, not designated
// initializers — Courier::Config has a constructor with default args
// (so it's not an aggregate), and ESP-IDF's strict gnu++2b + -Werror
// rejects designated initializers on non-aggregate types.
Courier::Config courier;
courier.host = "resident.inanimate.tech";
cfg.network  = courier;
```

**Header changes:**
- `<ResidentDevice.h>` → `<Resident.h>` (umbrella) or `<ResidentSandbox.h>`.
- `<ResidentDeviceConfig.h>` removed; use `<ResidentSandboxConfig.h>`
  (or `<Resident.h>`).

**`onMessage` semantics changed.** Resident now routes the reserved types
(`app`, `shader`, `app_event`) internally. Your `sandbox.onMessage(cb)`
callback only fires for unknown message types — the previous super-call
pattern (`Device::onMessage(...)` to keep sandbox routing) is no longer
needed and will not compile.

**Network customisation** (TLS cert, custom headers, additional transports,
custom WiFi configuration) is now done via `sandbox.onConfigureNetwork(cb)`,
which receives `Courier::Client&` directly. The previous pass-through
fields on `DeviceConfig` (`dns1`/`dns2`) move into `cfg.network` since
that field IS a `Courier::Config`. New Courier features become available
without a Resident change.

### Why

The old surface had three problems:
1. Story/code mismatch — landing page said "Sandbox", main.cpp said "Device".
2. `Resident::Device` overclaimed scope — it's a component on the board, not the board.
3. Different extension idiom from Courier (virtuals vs. callbacks) forced
   developers to switch mental models when crossing the boundary.

The single-class + callback model fixes all three.

### Migration

**See [`docs/migration-0.4-to-0.5.md`](migration-0.4-to-0.5.md) for the full
translation reference** — sectioned by topic, with old-vs-new tables and
before/after code blocks for every pattern. Mechanical in most cases.

The four example-project commits in this release are also worked references:
- `examples/m5stick-demo/device/src/main.cpp`
- `examples/adafruit-esp32-s2-feather/device/src/main.cpp`
- `examples/adafruit-esp32-s2-feather/device-minimal-resident/src/main.cpp`
- `examples/espidf-basic/main/main.cpp`

## v0.4.1-dev (dddec28)

Theme: tracking the courier 0.4 surface — namespaced types, JSON-first send,
per-transport hooks, and per-transport endpoint state.

### Breaking changes

**Requires courier `^0.4.1`.** Resident's `Device` API mirrors courier's
public surface. Read courier's migration guide
(`docs/migration-0.3-to-0.4.md` in the courier repo) before migrating
downstream code.

**`Resident::Device` callback and accessor types tracked the courier rename:**

- `Device::onMessage(const char* type, JsonDocument& doc)` →
  `Device::onMessage(const char* transportName, const char* type, JsonDocument& doc)`.
  The new first argument identifies which transport delivered the message
  (`"ws"`, `"mqtt"`, etc.). Every subclass override must update its signature
  and the call to `Device::onMessage(...)` super.
- `Device::onConnectionChange(CourierState)` →
  `Device::onConnectionChange(Courier::State)`. The enum is now scoped
  (`Courier::State::Connected`, etc.) — switch statements must update
  case labels.
- `Device::courier()` returns `Courier::Client&` (was `Courier&`).
- The protected `_ws` member is `Courier::WebSocketTransport&`
  (was `CourierWSTransport&`). Subclasses that referenced the type name
  directly (e.g. for `onConfigure(...)`) need to update.

**`Device::send`, `Device::sendTo`, `Device::sendBinaryTo` removed.** These
were thin passthroughs to courier methods that no longer exist. Replace at
the call site:

- JSON via the default transport: `device.courier().send(doc)` (where `doc`
  is a `JsonDocument`).
- Raw text or binary on a specific transport:
  `device.courier().transport<Courier::WebSocketTransport>(name).sendText(s)`
  / `.sendBinary(d, len)`.
- MQTT publish: `device.courier().transport<Courier::MqttTransport>(name).publish(topic, payload)`.

**`Device::buildWebSocketPath()` removed.** Override
`Device::onTransportsWillConnect()` instead and call
`_ws.setEndpoint(host, port, path)` directly. The hook fires after WiFi is
up and before transports begin connecting, on every connect cycle (initial
and reconnect), so any dynamic state (e.g. roomId from a registration HTTP
call) can be resolved fresh each time. The default `Device` implementation
sets the built-in WS endpoint to `/agents/<deviceType>-agent/<deviceId>`;
subclasses replace or extend it. This also lets a subclass register and
configure additional WS transports in the same hook (multiple WebSocket
endpoints on one device — previously awkward).

Migration sketch for a subclass that used `buildWebSocketPath()` to return
a static path:

```cpp
// Before
String buildWebSocketPath() override { return "/agents/foo/bar"; }

// After
void onTransportsWillConnect() override {
    _ws.setEndpoint(_config.host, 443, "/agents/foo/bar");
}
```

---

## v0.3.0-dev (d27cda1)

### Breaking changes

**Project renamed: Outrun → Resident.** All consumers must update:

1. `lib_deps` URL: `inanimate-tech/outrun` → `inanimate-tech/resident`.
2. ESP-IDF: `idf_component.yml` dependency name from `inanimate/outrun`
   to `inanimate/resident`.
3. C++ namespace: `Outrun::*` → `Resident::*` everywhere.
4. Include directives: `<OutrunX.h>` → `<ResidentX.h>`.
5. Cloudflare deployment hostname (m5stick-demo example):
   `outrun-m5stick-demo.*` → `resident-m5stick-demo.*`.

**Driver API rework.** Drivers now extend `Resident::Extension` (shared
lifecycle base) instead of overriding `installSandboxModule(lua_State*)`
directly, and register declaratively at config time. To migrate a driver:

1. Replace `installSandboxModule(lua_State*)` with
   `registerModule(Resident::LuaModule&)`. Bind each Lua function with
   `m.method<Class, &Class::fn>("name")`. Member functions take a
   `lua_State*`; `this` is recovered automatically. The old
   `getFromLua` static helper goes away.
2. Replace `sandbox().addDriver(&driver)` calls with
   `cfg.extensions = {&a, &b, ...}` in your `DeviceConfig` (or
   `SandboxConfig` for standalone use). `addDriver`, `addModule`, and
   `setShaderTemplate` are removed; use `cfg.shaderTemplate = fn` instead.
3. Delete manual `driver.begin()` / `driver.update()` calls from `setup()`
   and `loop()` — `Sandbox` calls them. Delete `sandbox().initialize()`
   from any `deviceSetup()` override — `Device::setup()` calls it after
   `deviceSetup()` returns.

Other API changes:

- `Resident::Module` class removed; things that were Modules now extend
  `Resident::Extension` directly.
- `Resident::StatusDisplay` gains optional `begin()` / `update()` virtuals
  (default no-op; existing implementations unaffected). `Device` drives
  them automatically.
- A `Driver` that also inherits `StatusDisplay` must list `Resident::Driver`
  first in its inheritance list (`class : public Driver, public
  StatusDisplay`) and should guard its `begin()` against double-call,
  since both `Device` and `Sandbox` reach it.
- Maximum 8 extensions per `Sandbox` (`Resident::Extensions::MAX`).

**ESP-IDF consumers only — registry vs. vendored deps:** `CMakeLists.txt`
`REQUIRES` defaults to the namespaced names exposed by the ESP Component
Registry (`inanimate__courier`, `bblanchon__arduinojson`). PlatformIO
consumers are unaffected.

If you vendor courier / ArduinoJson / ezTime / Esp32Lua under bare directory
names rather than fetching them via the registry, override the names from
your project's root `CMakeLists.txt`:

~~~cmake
set(RESIDENT_COURIER_DEP     "courier"     CACHE STRING "" FORCE)
set(RESIDENT_ARDUINOJSON_DEP "ArduinoJson" CACHE STRING "" FORCE)
~~~

`RESIDENT_EZTIME_DEP` defaults empty — `inanimate__courier` bundles ezTime
via CMake FetchContent on the registry path, so no separate component is
needed. Vendoring consumers who manage ezTime as a standalone component
should set `RESIDENT_EZTIME_DEP=ezTime` to satisfy the strict
header-required-component check.

`RESIDENT_ESP32LUA_DEP` defaults bare (`Esp32Lua`); the example's
`tools/fetch-deps.sh` fetches it locally since it's not on the registry.

### New features

- `Resident::LuaModule` builder: `method<Class, &Class::fn>("name")`,
  `staticMethod`, `constant`. Const member functions supported.
  C++14-compatible — no compiler-flag changes needed in downstream
  `platformio.ini`.
- `Resident::Extension` base class for Lua-only modules that have no
  hardware and emit no events. Same `registerModule` / lifecycle hooks
  as `Driver`.
- `Resident::Extensions` declarative wrapper: `cfg.extensions = {&a, &b}`.
- `examples/espidf-basic/`: minimal ESP-IDF example demonstrating the
  new declarative pattern. Uses `tools/fetch-deps.sh` to fetch
  `Esp32Lua` (the only dep not on the ESP Component Registry).
- Resident's `idf_component.yml` declares its registry dependencies, so
  IDF consumers no longer have to re-declare `arduino-esp32`, `courier`,
  or `arduinojson`.

### Fixes

- Driver `update()` runs at full main-loop rate even when no app is
  loaded, so button drivers keep debouncing between app reloads.
- Driver event-sink is wired before `begin()` is called, so drivers can
  safely report initial state via `sendEvent()` from `begin()`.

### Internal

- Added `tools/run-tests.py` (uv inline-script) and
  `.github/workflows/ci.yml` with four jobs: static analysis (cppcheck),
  unit tests (PIO native), PlatformIO build of `m5stick-demo`, ESP-IDF
  build of `examples/espidf-basic` against IDF v5.5.3.
- Native unit-test environment under `test/unit/` links Lua and provides
  direct test coverage for `Extension`, `LuaModule`, and `Extensions`.
- `examples/m5stick-demo/device/platformio.ini` patched to use the in-tree
  Resident source (`symlink://../../..`) and HTTPS for courier so the demo
  builds in CI without SSH credentials.

---

## v0.2.0-dev (82a34e4)

Initial version of this changelog. The state of the repo at commit `82a34e4` is the baseline — prior history is not retroactively documented here.

---

## Usage (for agents)

### Consuming Resident

Resident is a foundational library that other projects build on. If you are an agent working in a downstream project that depends on Resident:

1. Check the version of Resident your project currently uses (look at the dependency pin in your project's manifest, or the vendored copy's `library.json` / `idf_component.yml`).
2. Check the latest version of Resident in this changelog.
3. Read every section between those two versions and update your project's code accordingly — paying particular attention to **Breaking changes**.

### Updating this changelog

Each version section is headed `## vX.Y.Z-dev (<git-hash>)`, where `<git-hash>` is the short hash of the commit that introduced the section (or the most recent commit it covers, if updated in place).

Standard subsections, in order, omitting any that are empty:

- **Breaking changes** — API changes that require downstream code updates.
- **New features** — additions that are backward-compatible.
- **Fixes** — bug fixes.
- **Internal** — refactors, tooling, tests, docs — anything not visible to consumers.

A `-dev` version section is a work-in-progress: continue appending to it as work lands. When a semver version is **struck** (the `-dev` suffix is removed and the version is released), that section is frozen — do not modify it. New work then opens a fresh `## vX.Y.Z-dev (<git-hash>)` section above it.
