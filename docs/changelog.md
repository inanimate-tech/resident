# Changelog

## v0.8.0

Theme: the device speaks first. A device hello announces protocol, limits and
identity on every connect; telemetry and events ride channelled frames with a
queue behind them; the sandbox closes (fresh environment, closed stdlib,
dispatch deadline) and grows a persistent store, in-place chunk updates,
framework hosting, and library-agnostic render targets.

### Breaking changes

- Driver events deliver their payload as `event.data`, matching wire events; top-level scalars are still mirrored onto the event table for one deprecation window.
- A Lua dispatch that runs longer than `SandboxConfig::executionDeadlineMs` — new, and defaulting to 1000 ms — aborts with `runtime_error` while the app keeps running; set it to `0` for the unguarded 0.7 behaviour.
- Ticking and event dispatch no longer gate on connectivity; set `SandboxConfig::gateTickOnConnection = true` to gate them again.
- The app environment no longer gets `os`/`io`/`package`/`require`/`load`/`dofile`/`loadstring`/`loadfile`/`debug`; set `SandboxConfig::openUnsafeLibs = true` to open them.
- `loadApp` resets globals to the runtime baseline; set `SandboxConfig::freshAppEnvironment = false` for apps that relied on cross-load leakage.
- Once a host hello has been received, un-channelled messages and the `app_event` wrapper are dropped and counted instead of routed.
- `events.send` returns `"sent"`/`"queued"`/`"dropped"` instead of a boolean (both success states are truthy); C++ `publishEvent` keeps its boolean shape and `publishEventEx` exposes the three states.
- An oversized `events.send` payload drops the event instead of truncating it, on both the outgoing and incoming paths.
- `LvglModule::addDisplay(name[, DisplayOptions])` replaces `addDisplay(name, lv_display_t*, shape)` — the module creates the display, and boards register a `PanelTarget` instead of writing LVGL glue.

### New features

- **The device hello**: every transport connect queues `{channel:"system", type:"hello"}` carrying `protocol` (`RESIDENT_PROTOCOL_VERSION`), `deviceType`, `bootId`, the build's `limits`, `framework`, the running app's `storeNs`/`generationId`, and the optional `firmware`/`profile` from `SandboxConfig::firmwareVersion`/`profileRef`.
- An inbound host hello applies `data.tz` and is reported by `hostHelloSeen()`; `requestHello()` re-queues the device hello.
- Telemetry rides the wire as `{channel:"system", type:"telemetry"}`, queued and drained from `loop()`; `setSystemSink(fn)` is the control-plane mirror of `setEventSink`.
- Outbound events queue when rate-limited or offline (`RESIDENT_EVENT_QUEUE_SIZE`, default 16) and drain in order; `opts.keep = true` marks a message overflow eviction never removes.
- **Framework modules**: `SandboxConfig::framework = {name, version, source}` hosts privileged Lua outside the app, with a private environment, the `runtime.send` capability, and `framework_install`/`framework_tick`/`framework_event`/`framework_app_loaded` hooks.
- The framework slot — `{channel:"system", type:"framework", name, version, code}` — persists a replacement that wins over the built-in at boot; empty `code` reverts.
- **Wall-clock dispatch deadline**: `SandboxConfig::executionDeadlineMs` (default 1000 ms, 0 = off) aborts a dispatch that outlives it with `runtime_error` and leaves the app running; the guard is lazily armed, so a dispatch that keeps to its deadline runs with no Lua hook at all.
- **Slow-dispatch reporting**: `SandboxConfig::executionSoftDeadlineMs` (default 0 = off) reports a dispatch that outlives it — a rate-limited serial line plus a `slow_dispatch` telemetry event — without aborting it.
- **Lua `store` module**: `store.get/set/keys/clear/remaining` over scalars, persisted to NVS under `resident/store` and namespaced by the load message's `storeNs` (same namespace survives `loadApp` and reboot; a different one clears the slot).
- **`loadChunk(code)`** / `{channel:"system", type:"chunk"}` runs a chunk in the running app's `lua_State` — globals, timers and events survive, `init()` is not re-called, and a failing chunk leaves the app running.
- **Render targets**: `Resident::RenderTargets` + `PanelTarget` (geometry plus a synchronous big-endian RGB565 `blit`) is the one place a board declares its panels; the graphics modules build their own machinery over it.
- **Bind is the claim**: `lgfx.bind` / `lvgl.bind` take ownership of a target (last bind wins), the non-owner's present path stands down silently, and `onAppReset` releases every claim.
- Nothing measures a panel at registration time — `RenderTargets::addPanel` and both graphics modules' `addDisplay` declare only, because a board's config function commonly runs during static init where the display object does not exist yet (a LoadProhibited crash in global-ctor time on ESP32-S3 / core 3.3.9, with no serial output to explain it). Geometry is read at call time; `RenderTargets::declare(name, shape, module)` is the no-geometry registration a module uses, and `LgfxModule::begin()` records a sprite-backed target's size once hardware is up.
- **Lua `surfaces` module**: `surfaces.list()` / `surfaces.get(name)` read the render-target registry back — `{name, w, h, shape}` per panel, geometry from the `PanelTarget` at call time — so a consumer can ask a device what surfaces it has instead of being told out of band.
- **Lua `lgfx` module** (`ResidentLgfxModule.h`, opt-in): idiomatic LovyanGFX drawing — rect/roundRect/circle/triangle/line/pixel, text, `flip()` — with `LgfxSpriteTarget<T>` owning the frame buffer.
- **Lua `lvgl` module** (`ResidentLvglModule.h`, opt-in): retained-mode UI over LVGL 9 + luavgl; the module owns `lv_init`, the tick, the display, its buffers, the flush and the timer pump.
- **Capture brackets**: `startCapture(stream, format)` / `endCapture()` wrap the mic stream in `{channel:"system", type:"capture"}` frames while the media payloads stay raw.
- Outgoing frames stamp `src:"device"` and a per-boot monotonic `seq`; incoming events expose `event.channel` (always) plus `event.src`/`event.seq` when the frame carried them.
- `channel:"runtime"` frames route into the app event queue exactly like `channel:"app"`; `sendAppEvent(name, dataJson[, channel])` takes an optional channel.
- Event `data` is parsed with ArduinoJson on both paths: unescaped strings, integers as integers, floats, booleans, and nested containers to 3 levels.
- `events.send` serializes escaped keys/values, booleans and nested tables (objects and arrays) into a `RESIDENT_EVENT_JSON_MAX` buffer (default 1024, build-flag overridable).
- `ctx.generation_id` carries the `generationId` the load message stamped; `nil` for direct C++ loads and NVS restores.
- `on_event`'s ctx carries the wall-clock fields (`utc_h`/`utc_m`/`localtime_h`/`localtime_m`) like `init` and `on_tick`.
- **Drop accounting**: every silent-loss site feeds one counter, reported as `dropped` telemetry (`data.count` since boot) at most once a minute and only when changed.
- An over-budget `store.set` emits `store_full` telemetry with the key, once per key per app load.
- `Extensions::MAX` is 12 (was 8); the event ring depth is a build flag (`RESIDENT_EVENT_RING_SIZE`, default 8 slots / 7 usable).
- **`prompts/`** — canonical authoring sheets (`sandbox.md`, `lgfx.md`, `lvgl.md`) maintained next to the code they document; see `prompts/README.md` for the composition rules.
- The m5stick-demo `PushButtonsDriver` emits `tap {index, count}` and `hold {index, held}` alongside the existing `button` event.
- m5stick-demo gains an optional `m5stick-lvgl` build env, gated in `run-tests.py build` and therefore CI.

### Fixes

- The store flushes at least every 30 s while dirty (the quiet-only debounce never fired for an app that mutates on every tick), and flushes at capture start so a crash mid-session loses nothing.
- The `m5stick-lvgl` env pins the Inanimate luavgl fork's `main` (pin a commit SHA for a frozen build); the integration branch it used to name is gone, so that env could no longer fetch its dependency at all.

---

## v0.7.0

Theme: channel-based message routing — an envelope `channel` field steers messages onto a data plane, a control plane, or a custom slot, replacing the single flat `onMessage`/reserved-type dispatch as the routing new senders should use. Also in this release: `SystemMic` becomes a standalone capture interface with a shipped M5 driver.

### Dependencies

- **Courier `^0.6.0` on both registries** (was `^0.5.1`). 0.6.0 makes `Client::onMessage` deliver only the default transport's messages ("receive parallels send", mirroring `Client::send`) — channel routing needs one coherent per-transport message stream instead of every transport's traffic funnelled through the client-level callback.

### Breaking changes

- **`SystemMic` is no longer a `Driver` — it is a standalone capture
  interface, converged with hawthorn-firmware's `HawthornMicrophone`** (which
  it replaces downstream). Three backends with radically different internal
  models — synchronous I2S, M5Unified's asynchronous request queue, and a
  multi-mic array with a processing stage — had independently converged on the
  same three methods, and the `Driver` inheritance bought the mic nothing (no
  Lua surface, no events, no per-loop update) while forcing `void begin()`:
  - `begin()` now returns **`bool`** (false = capture hardware not acquired)
    and must be benign to call while already capturing.
  - New **`end()`** (default no-op): stop capture and release the hardware —
    for boards where mic and speaker share one full-duplex codec.
  - `frameSamples()` gains a default (`512`, the pump's cap); `sampleRate()`
    stays pure virtual. `name()`, `update()` and the rest of the
    `Driver`/`Extension` surface are gone.
  - The sandbox no longer runs the mic in its extension lifecycle: **the mic
    pump owns capture**. `startMicStream()` calls `begin()` — and now returns
    `bool`, false when there is no `systemMic` or it fails to start —
    `stopMicStream()` calls `end()`. Capture hardware is held only while
    streaming; expect ~2 frames of priming latency at stream start.

  Migrating a driver: change the base-class method signatures, move any
  capture-start out of setup-time assumptions, and delete `name()`. The
  contract in `ResidentSystemMic.h` (also
  [api.md](api.md#writing-a-mic-adaptor)) now has five rules — capture runs
  `begin()`→`end()` and `read()` only drains; never hand capture hardware the
  caller's buffer; `timeoutMs == 0` means do not block; short reads are
  legal; single caller.

### New features

- **`Resident::M5Mic`: a shipped, production-grade mic driver for M5 boards**
  (`#include <ResidentM5Mic.h>` — opt-in, not pulled in by `Resident.h`, and
  M5Unified stays *your* project's dependency, not Resident's). Wiring a mic
  on an M5StickS3 / M5StickC Plus2 / M5Stack is now two lines:

  ```cpp
  Resident::M5Mic mic;
  cfg.systemMic = &mic;
  ```

  Internally it is the three-buffer rotation described under Fixes below,
  plus the mic task pinned to core 1 at priority 18 (fixes a DMA-ring overrun
  under TLS streaming load at M5Unified's default priority 2) and `audit()`
  pipeline-health counters (`underruns` / `tornSlots` / `timeouts`) for field
  diagnosis. Ported from the Hawthorn firmware's on-device-validated
  `M5Microphone`, with two deliberate changes: `timeoutMs == 0` is a
  non-blocking poll per the contract (was: block indefinitely), and underrun
  detection is queue-occupancy-based rather than wait-duration-based, so it
  works under both blocking and non-blocking callers.

  The `m5stick-voice` example's hand-rolled `M5MicDriver.h` is deleted in
  favour of it.

- **Channel routing.** Incoming messages carry an envelope `channel` field:
  - **`channel:"app"` (data plane).** Every message routes to `handleAppMessage` → Lua `on_event(ctx, event)` with `event.name` set to `type`. No reserved types on this channel — `type:"forget"` here is just an event, not a persistence op. Self-echo is dropped (`from == getDeviceId()`, guards multicast loopback) and duplicates are deduped by `nonce` against a 16-entry ring (guards the same event arriving over more than one transport). Gated exactly like the existing `sendAppEvent`: dropped with no app loaded or no `on_event` handler, so the event ring can't leak stale events into whatever app loads next. The legacy `app_event` envelope is still accepted here for one release (`[deprecated] app_event wrapper; send channel:"app" with type=<event name>`).
  - **`channel:"system"` (control plane).** Reserved types `app`/`shader`/`forget` are handled exactly as before (including `deferAppLoads` and the new description display, below); any other type falls through to a `"system"` channel slot.
  - **Any other `channel` value** routes to a per-channel slot registered via `Sandbox::onMessageWithChannel(name, cb)` — up to 8 slots, exact-string match, last registration wins. An unregistered channel is logged and dropped.
  - **No `channel` field** takes the legacy un-channelled path — logs `[deprecated] un-channelled '<type>' message; sender should stamp channel`, then routes exactly as before (`onMessageFilter` → deferral → reserved-type routing → `onMessage`).
  - New public API: `Sandbox::handleAppMessage` / `handleSystemMessage` (callable directly by wrappers with their own receive path — e.g. per-topic MQTT hooks — with no loopback through Courier) and `Sandbox::onMessageWithChannel`.
- **`Sandbox::publishEvent(name, dataJson)`** — builds the `channel:"app"` envelope (`type`, `data`, `from`, `nonce`, `ts_ms`) and hands it to the event sink (`setEventSink(EventSink)` if set, otherwise `courier().send` on the default transport). Rate-limited with a token bucket (5 events/s sustained, burst of 10); returns `false` on rate limit, no sink/network, or send failure. This is the shared implementation behind the new Lua `events.send` and any C++ caller (e.g. a platform wrapper's `room.announce` alias) — **a deliberate behavior change** from the old `room.announce`, which raised a Lua error on rate limit rather than returning `false`.
- **`events` Lua module.** `events.send(name [, data])` publishes on the app data plane via `publishEvent` and returns a boolean. `data`, if given, must be a flat table of string/number values (other value types are silently skipped); serialized into a bounded 256-byte JSON buffer.
- **`Sandbox::sendSystem(doc)`** — stamps `channel:"system"` and sends via the default transport. For device control messages (voice start/end, etc.). Returns `false` with no network configured or on send failure; the doc is stamped either way.
- **Description-on-load display.** An `app`/`shader` load message (legacy path or `"system"` channel) carrying a `description` field is shown on `systemDisplay` at receipt, before any `deferAppLoads` stash is applied. On by default; `Sandbox::setShowDescriptions(false)` disables it — e.g. for devices whose `systemDisplay` is the main app screen.

### Fixes

- **`Sandbox` now defaults a networked `Courier::Config::defaultTransport` to `"ws"` when unset and `host` is set** — under 0.6's "receive parallels send", an unset default transport meant `Client::onMessage` (and thus all channel routing) never fired on any real device; callers who set an explicit `defaultTransport` are unaffected.

- **`m5stick-voice`'s `M5MicDriver` returned unfilled audio.** `M5.Mic.record()`
  is asynchronous — it queues a destination buffer into M5Unified's request
  queue and returns while the mic task fills it in the background — but the
  driver returned `maxSamples` the instant it returned. Every read handed back
  a buffer that had not been written yet, and because the destination was the
  *caller's* buffer, the mic task went on writing memory the caller already
  considered its own.

  Rewritten as a three-buffer rotation that derives completion from `record()`
  itself: M5Unified's queue holds exactly two requests and `record()` blocks
  until its slot is free, so `record()` returning means the request queued two
  calls earlier has completed — a guarantee, with nothing to poll. Queueing
  before serving also keeps two requests outstanding at all times, which
  matters: when its queue runs empty the mic task parks and silently discards
  the partially consumed DMA chunk it was holding, splicing the audio. The old
  one-buffer-per-read shape hit that on every read. The mic task is also now
  pinned to core 1 at priority 18, which fixes a separate DMA-ring overrun
  under TLS streaming load at M5Unified's default priority 2.

  Note `isRecording()` is *not* a completion signal — it is gated on a flag the
  mic task sets itself, so a freshly queued request reads back as "nothing
  pending" and a wait on it falls straight through onto an unfilled buffer. It
  answers queue occupancy, and is used only for that.

  (The rewritten driver has since moved out of the example and into Resident
  itself as `Resident::M5Mic` — see New features.)

### Deprecations

- **Un-channelled message routing** (no `channel` field) still works but now logs `[deprecated] un-channelled '<type>' message; sender should stamp channel` on every message. Stamp `channel:"app"`/`"system"`/a custom name instead.
- **The `app_event` envelope** on the app channel still works but now logs `[deprecated] app_event wrapper; send channel:"app" with type=<event name>`.
- **`Sandbox::onMessage` / `onMessageFilter`** now serve the legacy un-channelled path only (doc comments updated accordingly) — new code should use `onMessageWithChannel` / `handleAppMessage` / `handleSystemMessage`. Not yet `[[deprecated]]`-attributed: platform wrappers (e.g. Hawthorn's `HawthornRoomDevice`) still register the filter, and SDK examples may still use `onMessage`; attributes land when that shim is removed.

### Internal

- **`Resident::SystemMic::read()` now states its contract**, in
  `ResidentSystemMic.h` and [api.md](api.md#writing-a-mic-adaptor). The four rules
  each correspond to a defect seen in a real driver: capture belongs in
  `begin()` rather than `read()`; never hand capture hardware the caller's
  buffer; `timeoutMs == 0` means *do not block* (the pump calls
  `read(buf, frameSamples(), 0)` from `loop()`, so blocking stalls the whole
  sandbox); and short reads — including `0` — are legal and routine.

  Previously `timeoutMs == 0` was undefined, and independent implementations
  had read it as "don't block", "block indefinitely" and "ignore the parameter
  entirely". No runtime behaviour changed — the pump already passed `0` — but
  that is now the documented, tested contract rather than an accident.

- **Mic pump contract tests** (`test/unit/test/test_mic_stream/`) covering the
  rules the pump is responsible for upholding: it reads with `timeoutMs == 0`,
  forwards short reads verbatim rather than padding to `maxSamples`, emits
  nothing when a read returns `0`, and owns the mic's `begin()`/`end()` —
  begun once per stream start (not at `setup()`), ended on stop, re-begun on
  restart, and never left "streaming" when `begin()` fails.

---

## v0.6.2

### Dependencies

- **Courier `^0.5.1` on both registries** (was `^0.5.0`). 0.5.1 enables the
  IDF certificate bundle by default on the WS and MQTT transports; the surface
  Resident itself relies on is unchanged since 0.5.0.

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

Each version section is headed `## vX.Y.Z`, optionally opening with a one-paragraph theme.

Standard subsections, in order, omitting any that are empty:

- **Dependencies** — pin changes.
- **Breaking changes** — API changes that require downstream code updates.
- **New features** — additions that are backward-compatible.
- **Fixes** — bug fixes.
- **Internal** — refactors, tooling, tests, docs — anything not visible to consumers.

Every entry is **one line**: what changed and what a consumer does about it. Rationale belongs in the commit message, detail in `api.md`.

A `-dev` version section is a work-in-progress: continue appending to it as work lands, and keep `library.json` / `idf_component.yml` on the same `-dev` version. When the version is released (the `-dev` suffix is removed), that section is frozen — do not modify it. New work opens a fresh `## vX.Y.Z-dev` section above it.
