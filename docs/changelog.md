# Changelog

## v0.3.0-dev (d27cda1)

### Internal

- Added `tools/run-tests.py` (uv inline-script) with `static-analysis`,
  `unit`, `build`, and `all` subcommands. Local entry point and CI driver.
- Added `test/unit/` with a native PlatformIO env and a smoke assertion.
  Slot for future unit tests; no existing source is currently covered.
- Added `.github/workflows/ci.yml` with three jobs: static analysis
  (cppcheck), unit tests (PIO native), and PlatformIO build of
  `m5stick-demo`. A fourth job for ESP-IDF builds is planned but deferred
  pending an upstream `inanimate/courier` packaging fix (courier's CMake
  REQUIRES line names `ezTime`, `ArduinoJson`, `WiFiManager` by bare names
  but its `idf_component.yml` doesn't declare those deps, so consumers
  can't `idf.py reconfigure` against the published registry version).
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
