# Changelog

## v0.5.1-dev (77afc9a)

### New features

- **Device app persistence.** The last successfully-loaded app is saved to NVS
  and auto-reloaded on boot, behind a 20-second device-ID countdown screen. A
  saved app that no longer loads is discarded. New config: `persistApps`
  (default on), `systemButton`, `persistentStore`. New `clearPersistedApp()` /
  `{"type":"forget"}`.

- `Resident::Sandbox::suspendApp()` / `resumeApp()` / `isAppSuspended()` — pause
  and resume a running app's tick without unloading it. While suspended,
  `loop()` skips the Lua `on_tick`/event dispatch (Courier and extension updates
  keep running) and the status display is freed for direct text via
  `StatusDisplay::displayText()`. The m5stick-voice example uses it to show
  "Listening" over a running app during push-to-talk.

- **Unified driver lifecycle.** Role interfaces (`StatusDisplay`,
  `SystemButton`, `StatusLED`) are now `Driver` subclasses. Lifecycle is driven
  from one de-duplicated list: `begin()` once; `update()` every loop for
  role peripherals and (only while an app is loaded) for other extensions,
  independent of connectivity. Driver events are dropped when no app is loaded.
  **Migration:** any custom `StatusDisplay`, `SystemButton`, or `StatusLED`
  implementation must now also implement `Driver::name()` (a pure virtual
  returning a `const char*` identifier).

- **Idle-screen title.** `Sandbox::setIdleScreenTitle(const char*)` adds an
  optional line at the top of the idle screen — shown in both the resting Ready
  state and during the Pending boot countdown — above `Device ID` / `Type`.
  Internal: `showIdentityScreen` renamed to `showIdleScreen`.

- **Boot identity screen now waits for connectivity.** A networked device shows
  its connection status while connecting, then — once **connected** — the
  identity screen and (if an app is persisted) the 20-second countdown. A
  device that never connects stays on the connection screen and does not
  auto-load. Standalone devices show the identity/countdown immediately at
  setup, as before.

### Fixes

- **Lua allocator falls back to internal RAM on boards without PSRAM** (e.g. ESP32-S3FN8 / M5Dial). Previously every Lua allocation went to `MALLOC_CAP_SPIRAM`, which returns NULL when no PSRAM exists — the Lua runtime had no usable heap and every app failed with "not enough memory". The capability is now resolved once on first use: SPIRAM when present, internal 8-bit RAM otherwise. Boards with PSRAM are unaffected.

---

## v0.5.0

First public alpha.

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
