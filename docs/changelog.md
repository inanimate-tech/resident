# Changelog

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

**ESP-IDF consumers only:** `CMakeLists.txt` `REQUIRES` line now uses
namespaced component names — `inanimate__courier` and
`bblanchon__arduinojson` instead of `courier` and `ArduinoJson`. Projects
that vendor courier/ArduinoJson under bare names must rename them, vendor
under both, or switch to registry deps. PlatformIO consumers unaffected.

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
