# Changelog

## v0.6.2-dev

### Breaking changes

- **Overlay arbitration is now per surface, and the Overlay interface is
  reshaped.** Driven by the first multi-overlay consumer (Hawthorn devices
  with a voice overlay and an agent-status overlay, sometimes on different
  screens):
  - **Per-surface winners.** Overlays bound to the same surface contend by
    priority (ties: earlier registration); overlays on different surfaces are
    independent and can all show at once. Previously one global winner was
    drawn regardless of surface. A `nullptr` surface is now defined: a
    dedicated surface — contends with nothing, never suspends the app, no
    restore issued.
  - **`priority()` is no longer a virtual** — pass it to
    `addOverlay(overlay, surface, priority)` instead.
  - **`onActivate()` / `onDeactivate()` → `onAcquire()` / `onRelease()`** —
    an overlay is a *claim* on a surface; the names now say so.
  - **`Overlay::restore()` is gone; surfaces restore themselves.** New
    `SystemDisplay::restoreContent()` (default no-op) is called by the
    arbiter when a surface's last claim releases — uniformly, whether or not
    the app was suspended (previously restore only fired on the app-resume
    path, so non-suspending surfaces never restored). A handoff to another
    overlay on the same surface issues no restore.
  - **`onDraw(unsigned long dtMs)` is paced on the app-tick cadence**
    (10 FPS) instead of every `loop()` — the overlay takes over the
    suspended app's tick slot, and consumers no longer need hand-rolled
    draw throttling. `onAcquire` is the immediate first paint.

### New features

- **`Sandbox::onMessageFilter(cb)`** — single-slot pre-routing filter. Runs
  before app-load deferral, reserved-type routing (`app`/`shader`/
  `app_event`/`forget`), and the user `onMessage` callback; return `false`
  to consume the message. Platform wrappers (e.g. Hawthorn's
  HawthornRoomDevice) use this for dedup, self-echo filtering, and
  platform-only message types — previously they had to overwrite the
  sandbox's Courier `onMessage` hook and re-implement reserved-type routing
  by hand, which silently drifted as routing grew (the wrapper's copy
  predated `forget`, so `forget` was dead on those devices).
- **`Sandbox::deferAppLoads(bool)`** — while set, incoming `app`/`shader`
  messages are stashed (last one wins, heap-serialized at measured size)
  instead of loaded; clearing applies the stashed load immediately. For
  memory/CPU-sensitive windows like voice recording, where a Lua compile
  would stall the audio path. `hasDeferredAppLoad()` reports a pending
  stash.
- **`Sandbox::injectMessage(transportName, type, doc)`** — route a message
  through the sandbox exactly as if it arrived from a transport (filter →
  deferral → reserved-type routing → user callback). For wrappers with
  their own receive path, and for native tests.

### Fixes

- **An app pushed while a dual-role overlay claim is held now loads
  suspended** instead of ticking (and drawing) beneath the overlay, and the
  arbiter's suspension bookkeeping is reset when the previous app is stopped
  (previously a stale flag could leave the new app un-suspended).
- **A device-initiated `suspendApp()` now survives an overlay cycle**: the
  arbiter only resumes a suspension it performed itself.

---

## v0.6.1

### Dependencies

- **Courier `^0.5.0` on both registries.** Now that Courier 0.5.0 is published to the PlatformIO registry (previously only 0.4.2 was available there), `library.json` pins `inanimate/courier` to `^0.5.0` instead of tracking `main` via a git URL. Both the PlatformIO and ESP Component manifests now resolve Courier from their respective registries, so resident is fully version-pinned and safe to depend on from a registry.

---

## v0.6.0

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

- **Idle screen repainted after a persisted-app restore.** `finishBootCountdown`
  now repaints the resting idle screen after the countdown loads the app, so
  devices with a status display separate from the app screen no longer get stuck
  on the last countdown frame (no-op where the app owns the screen).

- **Boot identity screen now waits for connectivity.** A networked device shows
  its connection status while connecting, then — once **connected** — the
  identity screen and (if an app is persisted) the 20-second countdown. A
  device that never connects stays on the connection screen and does not
  auto-load. Standalone devices show the identity/countdown immediately at
  setup, as before.

- **Push-to-talk primitives (core).** Three device-agnostic building blocks,
  composable into hold-to-talk and voice-overlay experiences:
  - `Sandbox::onSystemButtonHold(cb)` — a runtime hold gesture on the
    `systemButton` role slot. `cb(true)` fires once past the threshold,
    `cb(false)` on release, whenever the device is not in the boot countdown
    (including `Ready` with no app loaded).
  - `SystemMic` role slot (`SandboxConfig::systemMic`) + an inline streaming
    pump: `startMicStream()` / `stopMicStream()` / `setMicStreamSink()` drain
    the mic each `loop()` and ship raw 16-bit mono PCM frames (default sink
    `ws().sendBinary`). No framing — control frames are a device concern.
  - **Overlay arbiter.** `Resident::Overlay` +
    `addOverlay(overlay, surface)` / `removeOverlay` / `requestOverlay`. The
    highest-priority requested overlay draws each `loop()`; the app is
    suspended while it shows **iff** its bound surface is dual-role (present in
    both `extensions[]` and a `system*` role slot) — so suspend is derived
    per-device, not declared on the overlay.

- **`status*` role slots renamed to `system*`.** `SandboxConfig::statusDisplay`
  / `statusLED` are renamed to `systemDisplay` / `systemLED`, matching
  `systemButton`. The `StatusDisplay` / `StatusLED` classes remain as plain
  (non-deprecated) aliases for `SystemDisplay` / `SystemLED` so existing driver
  subclasses keep compiling unchanged. **Migration:** the old config fields
  (`statusDisplay`, `statusLED`) still work but are `[[deprecated]]` — switch
  assignments to `systemDisplay` / `systemLED`.

- m5stick-voice migrated onto the new core primitives (system-button hold,
  overlay arbiter, SystemMic streaming pump); its hand-rolled push-to-talk
  orchestration (onHold + audio ring + telemetry) is removed.

### Fixes

- **Lua allocator falls back to internal RAM on boards without PSRAM** (e.g. ESP32-S3FN8 / M5Dial). Previously every Lua allocation went to `MALLOC_CAP_SPIRAM`, which returns NULL when no PSRAM exists — the Lua runtime had no usable heap and every app failed with "not enough memory". The capability is now resolved once on first use: SPIRAM when present, internal 8-bit RAM otherwise. Boards with PSRAM are unaffected.

### Dependencies

- **Courier 0.5.0.** The ESP Component Registry manifests now require `^0.5.0`, which brings NTP-first time sync (with a hardened HTTPS-Date fallback) and larger receivable payloads on no-PSRAM boards — directly benefiting pushed app code. The PlatformIO manifest continues to track Courier's `main` via git URL until 0.5.0 lands on the PlatformIO registry (currently 0.4.2).

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
