# Changelog

## v0.3.0-dev (d27cda1)

### Breaking changes (ESP-IDF consumers only)

**Outrun's `CMakeLists.txt` `REQUIRES` line now uses namespaced component
names: `inanimate__courier` and `bblanchon__arduinojson` instead of bare
`courier` and `ArduinoJson`.** This matches the names exposed by the ESP
component registry, so consumers using registry-resolved deps work out of
the box. Downstream IDF projects that vendor courier/ArduinoJson under
their bare names (e.g. `vendor/courier/`, `arduino-deps/ArduinoJson/`)
must rename them to the namespaced form, or vendor under both names, or
switch to registry deps. PlatformIO consumers are unaffected — PIO uses
`library.json`, not `CMakeLists.txt`.

### New features

- `examples/espidf-basic/`: minimal ESP-IDF example demonstrating Outrun
  integration in a real-consumer style. Uses registry pins for
  `espressif/arduino-esp32`, `inanimate/courier` (≥0.3.2), and
  `bblanchon/arduinojson`; a small `tools/fetch-deps.sh` script handles
  Esp32Lua (the only dep not on the ESP Component Registry, pinned by
  commit SHA against the upstream Arduino lib).
- Outrun's `idf_component.yml` now declares its dependencies on the
  registry (`espressif/arduino-esp32`, `inanimate/courier`,
  `bblanchon/arduinojson`), so IDF consumers no longer have to
  re-declare them. `Esp32Lua` remains consumer-supplied (not on the
  registry) — see the example's fetch script for the canonical pattern.

### Internal

- Added `tools/run-tests.py` (uv inline-script) with `static-analysis`,
  `unit`, `build`, and `all` subcommands. Local entry point and CI driver.
- Added `test/unit/` with a native PlatformIO env and a smoke assertion.
  Slot for future unit tests; no existing source is currently covered.
- Added `.github/workflows/ci.yml` with four jobs: static analysis
  (cppcheck), unit tests (PIO native), PlatformIO build of `m5stick-demo`,
  and ESP-IDF build of `examples/espidf-basic` against IDF v5.5.3.
- Patched `examples/m5stick-demo/device/platformio.ini` to use the in-tree
  Outrun source (`symlink://../../..`) and HTTPS for courier, so the demo
  builds in CI without SSH credentials. Added an explicit
  `symlink://lib/drivers` line to keep `M5StickDrivers` discoverable
  (PlatformIO suppresses the project's own `lib/` scan when a parent
  symlink contains the project). No functional change for local devs.

---

## v0.2.0-dev (82a34e4)

Initial version of this changelog. The state of the repo at commit `82a34e4` is the baseline — prior history is not retroactively documented here.

---

## Usage (for agents)

### Consuming Outrun

Outrun is a foundational library that other projects build on. If you are an agent working in a downstream project that depends on Outrun:

1. Check the version of Outrun your project currently uses (look at the dependency pin in your project's manifest, or the vendored copy's `library.json` / `idf_component.yml`).
2. Check the latest version of Outrun in this changelog.
3. Read every section between those two versions and update your project's code accordingly — paying particular attention to **Breaking changes**.

### Updating this changelog

Each version section is headed `## vX.Y.Z-dev (<git-hash>)`, where `<git-hash>` is the short hash of the commit that introduced the section (or the most recent commit it covers, if updated in place).

Standard subsections, in order, omitting any that are empty:

- **Breaking changes** — API changes that require downstream code updates.
- **New features** — additions that are backward-compatible.
- **Fixes** — bug fixes.
- **Internal** — refactors, tooling, tests, docs — anything not visible to consumers.

A `-dev` version section is a work-in-progress: continue appending to it as work lands. When a semver version is **struck** (the `-dev` suffix is removed and the version is released), that section is frozen — do not modify it. New work then opens a fresh `## vX.Y.Z-dev (<git-hash>)` section above it.
