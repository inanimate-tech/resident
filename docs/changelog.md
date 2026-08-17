# Changelog

## Unreleased

- **The hello slims to session mechanics.** The `surfaces` array and the `features` list are removed: surface/sensor/module facts are AUTHORING material whose one authority is the document behind the `profile` key, and the feature toggles gated nothing (chunk support is part of proto 1; telemetry is handled as it arrives; capture announces itself via its bracket). The hello carries exactly what a host needs to operate a session: `proto`, identity, `limits`, `framework`, `app`, and the `profile` key.

- **Render-target registry (arc R17).** New `Resident::RenderTargets` (`ResidentRenderTargets.h`, also in `Resident.h`): a static-capacity, allocation-free table where a board's drawable surfaces are declared — `{name, w, h, shape: "rect"|"round", modules bitmask}` — fed by both graphics modules' `addDisplay` (`LgfxModule` gains an optional `shape` parameter and re-reads geometry in `begin()`, since sprite-backed targets have none until their driver's begin; same-name registrations merge, module bits OR). The registry is INTERNAL machinery (bind-by-name resolution for the graphics modules) — deliberately not broadcast in the hello: surface geometry is an authoring fact, and the one authority for authoring facts is the document behind the hello's `profile` key. Entries are board-lifetime (`clear()` is for tests).
- **Lua `lvgl` module: retained-mode UI via LVGL 9 + the luavgl arc fork (arc R17).** New opt-in extension (`ResidentLvglModule.h`, header-only, NOT pulled in by `Resident.h` — the ResidentM5Mic pattern; it compiles to nothing without LVGL/luavgl present, so native tests and cppcheck are untouched): firmware registers `lv_display_t`s (`lvglModule.addDisplay("main", disp, shape?)`), Lua binds by name — `local h = lvgl.bind("main")` returns luavgl's display-scoped handle (`luavgl_bind_display`: widget constructors on the display's active screen, `set_theme`, multi-display; ownership/handle-invalidation stays luavgl's business at the display level). Because the sandbox registers each extension as a fresh global table, the `lvgl` global would shadow luavgl's own module (constants, `Font`, `Anim`, widget ctors); the module's table instead falls through via a metatable `__index` that resolves lazily against the registry `_LOADED` table — reachable with the package library closed to apps, populated when the first `bind` runs `luaL_requiref`. New `LuaModule::fallthrough(lua_CFunction)` supports the pattern. Authoring sheet: `prompts/lvgl.md` (every name verified against the fork source), composed per `prompts/README.md` — lvgl-capable boards append it to their DEVICE-SKILL composition.
- **m5stick-demo: optional `m5stick-lvgl` build env.** The vendored LVGL driver from the m5stick-arc reference is absorbed as `lib/drivers/LVGLDriver.{h,cpp}` — pure display/flush/tick glue now (lv_init, DMA draw buffer, byte-swap flush over M5GFX, millis tick, `lv_timer_handler` pump, app-reset tree wipe) that registers its `lv_display_t` into the new `LvglModule` instead of owning a Lua surface, wired in `main.cpp` under `HAS_LVGL`. **The arc C theme is stripped: themes now come from Lua** via the bound handle's `h:set_theme{...}` — glue drivers bake no taste. The env pins LVGL to the commit the luavgl fork tracks and luavgl to the fork's `#arc` integration branch (the display-bind work is merged there). The default `m5stick` env is untouched; the new env is build-gated in `run-tests.py build` (and therefore CI).

- **Framework modules.** The sandbox can now host privileged runtime code OUTSIDE the app: `SandboxConfig::framework = {name, version, source}` embeds a built-in framework whose chunk runs in a PRIVATE environment (`__index` → `_G`: it reads the baseline; bare assignments stay framework-local; `_G.x = ...` is the explicit app-facing install). The sandbox grants it — and only it — the runtime-channel sender (`runtime.send(name, data, opts)`, same queue semantics as `events.send`, channel pinned to `"runtime"`) and lifecycle interception via optional hooks: `framework_install()` (before each app chunk, after the fresh-environment reset), `framework_tick(ctx, dt)` (before the app's tick), `framework_event(ctx, e)` (before the app's `on_event`; return true to consume), `framework_app_loaded()`. Under a framework, apps may define NO lifecycle globals. **The framework slot**: `{channel:"system", type:"framework", name, version, code}` installs a persisted replacement (new `PersistentStore` virtuals `saveFramework`/`loadFramework`/`clearFramework`, NVS key `resident/framework`; inert defaults) that wins over the built-in at boot; empty `code` reverts; a bad blob is discarded with `framework_error` and the built-in runs; success emits `framework_applied`. The hello announces `framework: {name, version, source: "builtin"|"slot"}`. Resident stays generic: `name` is data the board supplies, never interpreted.
- **Execution budget.** Every dispatch (`init`, one tick, one event, one chunk, each framework hook) runs under a Lua instruction cap (`SandboxConfig::executionBudget`, default 2,000,000; 0 = unlimited): a runaway `while true do end` aborts THAT dispatch with a `runtime_error` ("execution budget exceeded") — the app and the device survive.

- **Fresh app environment per load.** `loadApp` now resets the globals to the runtime baseline (stdlib, math globals, `log`/`time`, driver + internal modules) — nothing from the previous app survives except the store slot. "Fresh boot" finally means fresh: a truthy leftover from the old app can no longer change the new app's behavior. Chunks deliberately keep the running environment (that is their point). Builds whose apps relied on cross-load leakage set `SandboxConfig::freshAppEnvironment = false`.

- **The events module owns delivery.** `events.send(name, data, opts?)` now returns `"sent" | "queued" | "dropped"` (both success states truthy): rate-limited or offline sends QUEUE (bounded, `RESIDENT_EVENT_QUEUE_SIZE`, default 16) and drain in order from `loop()` as tokens/connectivity allow. Envelopes are stamped (seq/nonce) at enqueue, so ordering holds and retries are dedup-safe. `opts.keep = true` marks a message that overflow eviction never removes (eviction prefers the oldest non-keeper and counts the drop). C++ `publishEvent` keeps its boolean shape (true = sent or queued); `publishEventEx` exposes the three states.
- **Capture brackets — one dialect.** New `Sandbox::startCapture(stream = 1, format = 1)` / `endCapture()`: sends `{channel:"system", type:"capture", data:{state, stream, format}}` around the mic stream — the bracket carries the metadata, the media payloads stay raw. A failed mic start closes the bracket it opened. `startMicStream`/`stopMicStream` remain for boards that own their own control frames. The m5stick-voice example now uses the brackets.
- **Recognition belongs to drivers.** The m5stick-demo `PushButtonsDriver` now emits `tap {index, count}` on release and `hold {index, held}` around long presses (with or without a firmware callback; default threshold 500 ms). The legacy `button` event still fires alongside `tap` for one release.

- **Offline-first.** Ticking and event dispatch no longer gate on connectivity — a disconnected device keeps running its app (timers fire, queued input dispatches); only network sends wait. Boards that relied on the gated behavior set `SandboxConfig::gateTickOnConnection = true`.
- **Closed stdlib by default.** The app environment no longer gets `os`, `io`, `package`/`require`, `load`, `dofile`, `loadstring`, `loadfile`, or `debug` — it is a sandbox. Trusted builds opt back in with `SandboxConfig::openUnsafeLibs = true`. The pure libraries (`string`/`table`/`math`/`coroutine`/`utf8`) and `collectgarbage` are unaffected.
- **Store budget feedback.** New `store.remaining()` (bytes left in the persisted budget), and an over-budget `store.set` now emits `store_full` telemetry with the key — once per key per app load. Silent state loss is no longer possible.
- **Drop accounting.** Every silent-loss site — event-ring overflow, oversize payloads in either direction, rate-limited publishes, closed legacy paths — feeds one counter, reported as `dropped` telemetry (`data.count` = cumulative since boot), throttled to one report per minute and only when changed. The event ring size is now a build flag (`RESIDENT_EVENT_RING_SIZE`, default 8 slots / 7 usable).
- **Legacy paths close when the host speaks hello.** Once a host hello has been received, un-channelled messages and the `app_event` wrapper are dropped-and-counted instead of routed — the reverse-hello rule from the receiving side. Hosts that never hello keep the legacy paths in full.

- **One event payload shape: `event.data` everywhere (driver flatten deprecated, still mirrored).** Every event — driver and wire alike — now carries its payload as an `event.data` table, parsed by the same ArduinoJson rules; the hand-rolled flatten parser (which silently dropped booleans and nesting, and cut strings at the first quote) is deleted, and `EventField` gains `FLOAT` and `BOOL` alongside `INT` and `STRING`. **Backwards compatible:** for a deprecation window, driver events ALSO mirror their top-level scalars onto the event table (the historical flattened shape; envelope keys — `name`/`from`/`ts_ms`/`channel`/`src`/`seq`/`data` — always win), so existing apps reading `e.index` — NVS-persisted ones included — survive a firmware bump unchanged. New code should read `event.data.index`; the shadow is removed in the next major. All shipped example apps, DEVICE-SKILL.md files, and prompt sheets teach the new shape. Firmware `sendEvent` call sites need no changes unless they want the new field types.
- **The device hello.** On every transport connect the sandbox queues `{channel:"system", type:"hello"}` announcing `proto` (new `RESIDENT_PROTO_VERSION`, 1), deviceType, `bootId` (per-boot random hex), the build's actual limits (`eventBytes`/`replyBytes`/`storeBytes`/`storeNsChars`/`eventsPerSec`), a feature list (`chunk`, `telemetry`, plus `media` when a `systemMic` is configured), optional board-supplied `firmware` and `profile` (new `SandboxConfig::firmwareVersion` / `profileRef`), and the running (or countdown-pending) app's `storeNs` + wire-stamped `generationId`. Public `requestHello()` re-queues it. Inbound host hello: `data.tz` is applied via `setTimezone`, receipt is exposed as `hostHelloSeen()` — nothing else gates on it yet (a host that never hellos gets today's behavior in full; future defaults key on this per the reverse-hello compatibility rule). Inbound `goodbye` is logged. See docs/api.md "Hello".
- **Telemetry rides the wire by default.** Every `emitTelemetry` now ALSO queues a channelled frame — `{channel:"system", type:"telemetry", data:{name, generationId?, error?}}` — drained from `loop()` (never sent from the receive context, where telemetry often fires: `loadApp` → `compile_error`). Bounded 8-slot queue (7 usable), oldest dropped while unsendable. The `TelemetryCallback` keeps its legacy flat format unchanged as an additional sink. New `setSystemSink(fn)` — the control-plane mirror of `setEventSink` — lets tests and platform wrappers capture `sendSystem` traffic (hello + telemetry included).
- **Uniform `ctx`.** `on_event`'s ctx now carries the wall-clock fields (`utc_h`/`utc_m`/`localtime_h`/`localtime_m`) like `init` and `on_tick` — a handler reading `ctx.localtime_h` used to get `nil` only there.
- **`prompts/` — canonical authoring sheets for coding agents.** `prompts/sandbox.md` (the universal Lua surface) and `prompts/lgfx.md` (the lgfx module), maintained next to the code they document and composed per board by consumers (agent skills, and any host that prompts models to write Resident apps); see `prompts/README.md` for the composition rules. The create-app skill's embedded copy now carries a provenance banner and is synced from the canonical sheet — fixing its stale `kv` module section (the module has been `store` since it shipped).
- **`Extensions::MAX` 8 → 12** (a fully loaded device used all 8 and the `lgfx` module needs a slot; `Sandbox::_lifecycle` is sized off the constant, so everything scales). **`ctx.generation_id`**: the system `{type:"app"}` load message gains optional `generationId` (string); when present it becomes the sandbox's generation id and is surfaced as `ctx.generation_id` in `init`/`on_tick`/`on_event` — `nil` when the load didn't carry one (direct C++ loads, NVS restores, which keep the self-generated telemetry-only id).
- **Lua `lgfx` module: idiomatic LovyanGFX drawing (arc A7).** New optional extension (`ResidentLgfxModule.h`, header-only): firmware registers displays (`lgfxModule.addDisplay("main", &target)`), Lua binds them (`local g = lgfx.bind("main")`) and draws with the core LovyanGFX set — fill/draw rect/roundRect/circle/triangle/line/pixel, fillScreen, setTextColor/Size/Datum, setCursor/print/drawString, width/height — colors as 24-bit `0xRRGGBB`, `g:flip()` presenting via a firmware-supplied callback (sprite push) or a no-op for direct-to-panel targets. The binding layer targets a minimal abstract `LgfxTarget`; `LgfxLovyanTarget<TGfx>` is a duck-typed header-only adapter for `LGFX_Device`/`LGFX_Sprite`/`M5Canvas`/test fakes. m5stick-demo wires it to the DisplayDriver's shared sprite.
- **Lua `store` module: a KV slot that survives `loadApp` (arc A4).** `store.get/set/keys/clear` over scalars (strings/numbers incl. integers/booleans; `nil` deletes), RAM-backed with **debounced** write-through to the persistent store (once per ~2 s of quiet, plus a forced flush on app unload — NVS wear). Persisted as one JSON blob `{"ns":...,"kv":{...}}` under NVS key `resident/store` (new `PersistentStore` virtuals `saveStore`/`loadStore`/`clearStore` with inert defaults — existing implementations keep compiling, RAM-only). Scoped by a server-provided app identity: the system `{type:"app"}` load message gains optional `storeNs` (≤32 chars; missing = shared default `"app"`); same ns = state survives loadApp and reboot, different ns = slot cleared first (that is the whole reset policy). Budget: `RESIDENT_STORE_JSON_MAX` (default 2 KB) on the serialized blob; `store.set` over budget returns `false` with no partial write.
- **`loadChunk`: in-sandbox chunk loading (the update lattice's middle rung).** `Sandbox::loadChunk(code)` compiles and runs a Lua chunk in the RUNNING app's `lua_State` — globals/timers/events survive, `init()` is not re-called, and a chunk that fails to compile or errors at run leaves the app running (Serial + `chunk_error` telemetry; success emits `chunk_applied`). A chunk that redefines `init`/`on_tick`/`on_event` takes effect on the next dispatch (cached refs are refreshed). Wire entry: system-channel `{type:"chunk", code:"..."}`. Chunks are never persisted to NVS (ephemeral by design — the server re-sends after reboot) and are **dropped with a log** (not stashed) during a `deferAppLoads` window.
- **Event envelope: channels through the sandbox boundary.** Outgoing frames (`publishEvent` / `events.send`) additionally stamp `src:"device"` and a per-boot monotonic uint32 `seq` (one counter per device-as-sender; the `nonce` suffix reuses the same count) — additive, existing fields unchanged. Incoming: `on_event`'s `event` table gains `event.channel` (always present — `"app"`/`"runtime"` for wire frames, `"driver"` for hardware events and host `sendAppEvent` injections) plus `event.src`/`event.seq` when the frame carried them. A new incoming `channel:"runtime"` routes into the sandbox event queue exactly like `"app"` (self-echo/nonce-dedup/app-running gates included); `system` remains firmware-only. C++ `sendAppEvent(name, dataJson[, channel])` gains an optional channel (default `"driver"`; the legacy un-channelled `app_event` wire path tags `"app"`). The Lua `events.send` signature is unchanged.
- **Incoming app-event data: real JSON parsing.** `processNextEvent`'s hand-rolled flat parser (which cut string values at the first `"` and dropped booleans/nesting) is replaced with ArduinoJson + a mirror of the outgoing rules: strings arrive unescaped, integers as Lua integers (previously all numbers were floats), floats as floats, booleans as booleans, nested objects/arrays as tables to 3 container levels (deeper containers skipped with their key; JSON `null` leaves a hole at its array index). Drop-don't-truncate on this side too: unparseable ring data drops the whole event (Serial log) instead of delivering it garbled, and incoming `data` larger than `RESIDENT_EVENT_JSON_MAX` is dropped at receipt (`handleAppMessage` / legacy `app_event`) rather than silently truncated by `serializeJson`. Driver events (`driverEventHandler`'s compact field format) keep their existing matched writer/parser pair.
- **`events.send` serializer upgrade.** The outgoing Lua-table → JSON serializer now JSON-escapes keys and values (`"`, `\`, `\n`, `\r`, `\t`, `\u00XX` for other control chars), supports booleans (`true`/`false`), and serializes nested tables to 3 table levels — string-keyed tables as objects, tables with a non-empty array part as arrays. Existing flat string/number payloads serialize byte-identically (escaping aside). The buffer is now `RESIDENT_EVENT_JSON_MAX` bytes (default 1024, build-flag overridable; was a fixed 256), and overflow **drops the event** (Serial log, `events.send` returns `false`) instead of truncating. The same constant now sizes the incoming app-channel `data` buffer and the event ring's per-slot `data` storage (RAM note: the 8-slot ring grows by 8× any increase — +6 KB at the default).

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

Each version section is headed `## vX.Y.Z-dev (<git-hash>)`, where `<git-hash>` is the short hash of the commit that introduced the section (or the most recent commit it covers, if updated in place).

Standard subsections, in order, omitting any that are empty:

- **Breaking changes** — API changes that require downstream code updates.
- **New features** — additions that are backward-compatible.
- **Fixes** — bug fixes.
- **Internal** — refactors, tooling, tests, docs — anything not visible to consumers.

A `-dev` version section is a work-in-progress: continue appending to it as work lands. When a semver version is **struck** (the `-dev` suffix is removed and the version is released), that section is frozen — do not modify it. New work then opens a fresh `## vX.Y.Z-dev (<git-hash>)` section above it.
