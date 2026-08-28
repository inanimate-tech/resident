# Resident API Reference

## Configuration

All configuration uses a struct-and-assign pattern compatible with C++11. `SandboxConfig` is in `ResidentSandboxConfig.h`, pulled in by the `Resident.h` umbrella.

For global instances, use a factory function so construction happens after static init:

```cpp
#include <Resident.h>

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType = "my-device";
    cfg.extensions = {&myDisplay, &myButton};

    // Courier::Config has a constructor with default args, so designated
    // initializers don't compile under strict ESP-IDF builds. Use direct
    // field assignment.
    Courier::Config courier;
    courier.host = "api.example.com";
    cfg.network  = courier;

    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() { sandbox.setup(); }
void loop()  { sandbox.loop(); }
```

### SandboxConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `deviceType` | `const char*` | `nullptr` | Device type string — used for the WiFiManager AP name and the default `/agents/<type>-agent/<deviceId>` WS path |
| `firmwareVersion` | `const char*` | `nullptr` | The board build's version string, announced in the device hello (see [Hello](#hello)). Omitted from the hello when null. |
| `profileRef` | `const char*` | `nullptr` | Name+version of this device type's out-of-band authoring document (e.g. `"m5stick@2"`), announced in the device hello. Omitted when null. |
| `extensions` | `Extensions` | `{}` | Drivers and extensions registered with the sandbox (registration order is preserved across `begin()` / `registerModule()` / `update()` / `onAppReset()`) |
| `shaderTemplate` | `ShaderTemplateFn` | `nullptr` | Function that converts shader fields into Lua source (see [Message Protocol](#message-protocol)) |
| `telemetry` | `TelemetryCallback` | `nullptr` | Called with outgoing telemetry JSON strings (also settable later via `sandbox.setTelemetryCallback`) |
| `timezone` | `const char*` | `nullptr` | IANA timezone string applied at construction (e.g. `"Europe/London"`); also settable later via `sandbox.setTimezone` |
| `systemDisplay` | `SystemDisplay*` | `nullptr` | Optional text display; Resident's internal handler calls `displayText()` automatically on connection state changes |
| `systemLED` | `SystemLED*` | `nullptr` | Optional LED indicator; Resident's internal handler calls `solidColor()` automatically on connection state changes |
| `network` | `std::optional<Courier::Config>` | unset | Networking opt-in. Set ⇒ Sandbox constructs an internal `Courier::Client`, drives WiFi / transports, fires connection callbacks. Unset ⇒ standalone runtime, no WiFi pulled in. |
| `persistApps` | `bool` | `true` | Save the last successfully-loaded app to flash and restore it on boot. Set to `false` to disable for a build. |
| `gateTickOnConnection` | `bool` | `false` | Pause `on_tick` and event dispatch while disconnected. The default is offline-first: the app keeps running; only network sends wait. |
| `openUnsafeLibs` | `bool` | `false` | Give the app environment the full Lua stdlib. The default removes `os`/`io`/`package`/`require`/`load`/`dofile`/`loadstring`/`loadfile`/`debug`. |
| `freshAppEnvironment` | `bool` | `true` | Reset app globals to the runtime baseline on every `loadApp` — nothing from the previous app survives except the store slot. Set `false` for builds whose apps need globals to persist across loads. |
| `executionBudget` | `uint32_t` | `2000000` | Lua instruction cap per dispatch (init / one tick / one event / one chunk / each framework hook). Over-budget aborts the dispatch with a `runtime_error`; the app survives. `0` = unlimited. |
| `executionDeadlineMs` | `uint32_t` | `0` | Per-dispatch **wall-clock** hard deadline, in milliseconds. Non-zero takes precedence over `executionBudget` and inverts how the guard is armed: the dispatch runs with no Lua hook at all, and a one-shot timer installs a stopping hook only once the deadline passes — so a dispatch that finishes in time pays nothing, where an instruction cap keeps the VM in trap mode for the whole dispatch. Over-deadline aborts the dispatch with a `runtime_error` (`execution deadline exceeded (N ms)`); the app survives. Device builds only (needs `esp_timer`); `0` = use `executionBudget`. |
| `executionSoftDeadlineMs` | `uint32_t` | `0` | Per-dispatch **wall-clock** soft deadline, in milliseconds. A dispatch that outlives it is reported — a rate-limited serial line plus a `slow_dispatch` telemetry event — and allowed to finish. Measured at dispatch end, so it also sees time spent inside a blocking C binding, which no Lua hook can observe. Independent of `executionDeadlineMs` and available on every platform; `0` = no reporting. |
| `framework` | `std::optional<FrameworkConfig>` | unset | Embed a framework module: `{name, version, source}` — privileged Lua the sandbox hosts outside the app (see [Framework modules](#framework-modules)). |
| `systemButton` | `Resident::SystemButton*` | `nullptr` | Optional button the runtime polls to skip the boot countdown (and, via `onSystemButtonHold`, a runtime hold gesture). Implement `Resident::SystemButton` and pass a pointer here. |
| `systemMic` | `Resident::SystemMic*` | `nullptr` | Optional microphone the runtime streams via the mic pump (see [SystemMic](#residentsystemmic)). On M5 boards use the shipped `Resident::M5Mic` (`#include <ResidentM5Mic.h>`); otherwise implement `Resident::SystemMic`. Not a `Driver` — the pump owns its `begin()`/`end()`. |
| `persistentStore` | `Resident::PersistentStore*` | `nullptr` | Override the backing store for persistence. `nullptr` uses NVS on device; inject a fake in tests. |
| `statusDisplay` | `SystemDisplay*` | `nullptr` | **Deprecated** — use `systemDisplay`. Kept as a fallback; a compile-time warning nudges migration. |
| `statusLED` | `SystemLED*` | `nullptr` | **Deprecated** — use `systemLED`. Kept as a fallback; a compile-time warning nudges migration. |

The `extensions` field is filled with a brace-list of `Extension*` pointers in registration order:

```cpp
cfg.extensions = {&displayDriver, &buttonDriver, &imuDriver};
```

The `network` field uses field assignment to avoid designated-initializer pitfalls:

```cpp
Courier::Config courier;
courier.host = "resident.inanimate.tech";
courier.port = 443;
cfg.network  = courier;
```

Omit `cfg.network` entirely to run standalone. `sandbox.hasNetwork()` reflects this choice; `sandbox.courier()` and `sandbox.ws()` assert if called on a no-network sandbox (programming error, not a recoverable runtime state).

---

## Resident::Sandbox

The single public Resident class. The Lua sandbox composed with optional [Courier](https://github.com/inanimate-tech/courier) connectivity.

Include with:

```cpp
#include <Resident.h>
```

### Construction

```cpp
Resident::SandboxConfig cfg;
// ... populate fields ...
Resident::Sandbox sandbox{cfg};
```

`Sandbox` is move-non-trivial (it holds Lua state, optional Courier, callback closures) — declare it as a global / static, not a stack local in `setup()`.

If `cfg.network` is set, the internal `Courier::Client` is constructed inside the `Sandbox` constructor and is available via `sandbox.courier()` immediately. Transports are not yet wired up — that happens in `setup()`.

### Lifecycle

```cpp
sandbox.setup();   // call from Arduino setup() (after peripheral init)
sandbox.loop();    // call from Arduino loop()
```

`setup()` is idempotent: a second call is a no-op.

### Lifecycle ordering

1. **Constructor** — applies the config. If `cfg.network` is set, the internal `Courier::Client` is constructed (but not started). If `cfg.timezone` is set, it is applied. If `cfg.telemetry` is set, it is stored.
2. **`setup()`** — in order:
   1. The user-registered `onConfigureNetwork(cb)` fires (if any), receiving the `Courier::Client&`. Use this to configure transports, register additional transports, or set TLS certificates.
   2. Resident's internal Courier hooks are wired (status-display / status-LED updates, reserved-type message routing).
   3. WiFiManager AP name is set to `"Resident <DeviceType> <id-suffix>"`.
   4. The sandbox initialises: Lua state is created, then Resident builds a de-duplicated lifecycle list of all managed objects — the `extensions[]` entries plus any role-slot peripherals (`systemDisplay`, `systemLED`, `systemButton`). Each object in that list receives `begin()` and `registerModule()` exactly once (idempotent), in registration order. Globals are registered.
   5. `Courier::Client::setup()` runs, which kicks WiFi and transports. During this:
      - `onCourierConnectionChange` fires for each state transition (`WifiConnecting` → `WifiConnected` → `TransportsConnecting` → `Connected`, etc.). Internal handler updates `systemDisplay`/`systemLED`, then the user's `onConnectionChange(cb)` callback fires.
      - `onCourierTransportsWillConnect` fires once before transports begin. Internal handler sets the default `/agents/<type>-agent/<id>` WS path, then the user's `onTransportsWillConnect(cb)` callback fires (override the path here).
      - `onCourierConnected` fires when fully connected. The user's `onConnected(cb)` callback runs.
3. **`loop()`** — in order:
   1. `Courier::Client::loop()` drives the network state machine and reads transports.
   2. **Driver heartbeat:** the de-duplicated lifecycle list is walked once and `update()` is called on each object. Role peripherals (`systemDisplay`, `systemLED`, `systemButton`) update every loop regardless of app state or connectivity. All other extensions update only while an app is loaded (running or suspended). Connectivity does not gate either cadence.
   3. If a persisted app is waiting to load, the boot countdown runs (and the function returns early).
   4. The Lua `on_tick(ctx, dt_ms)` callback fires at 10 FPS (100 ms interval) while an app is Running. Connectivity does not gate it unless `SandboxConfig::gateTickOnConnection` is set.
   5. Up to one pending event is delivered to `on_event(ctx, event)`.

### Setup-phase callbacks (register before `setup()`)

```cpp
sandbox.onConfigureNetwork([](Courier::Client& c) {
    c.transport<Courier::WebSocketTransport>("ws").onConfigure([](auto& t) {
        t.setRootCA(rootCertPem);
    });
});
```

| Callback | Signature | Fires |
|----------|-----------|-------|
| `onConfigureNetwork` | `void(Courier::Client&)` | Once at the top of `setup()`, before any Courier wiring. Use to configure existing transports (TLS certs, custom WiFi callbacks via `c.onConfigureWiFi(...)`) or to register additional transports (`c.addTransport<MqttTransport>(...)`). |

### Reactive callbacks (single-slot — last registration wins)

Register before or after `setup()`. Each call replaces the previous handler for that slot.

```cpp
sandbox.onTransportsWillConnect([]() {
    // Override the default WS endpoint:
    String path = String("/devices/") + sandbox.getDeviceId();
    sandbox.ws().setEndpoint("resident.inanimate.tech", 443, path.c_str());
});

// Legacy un-channelled path only — see Channel routing below for new code.
sandbox.onMessage([](const char* transport, const char* type, JsonDocument& doc) {
    if (strcmp(type, "config") == 0) {
        // handle a custom message type
    }
});

sandbox.onConnectionChange([](Courier::State state) {
    // fires on every state transition; systemDisplay/systemLED are already updated
});

sandbox.onConnected([]() {
    // fires when fully connected — good place to load a bootstrap app
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    sandbox.loadApp(bootstrapLua);
});
```

### Message interposition

Three tools for platform wrappers that need to sit in front of the sandbox's message routing without re-implementing it. Their reach differs: `onMessageFilter` is **legacy un-channelled path only**; `deferAppLoads` and `injectMessage` apply across both the legacy path and channel routing (see [Channel routing](#channel-routing)).

```cpp
// Pre-routing filter (single-slot), legacy un-channelled path only (no
// "channel" field). Runs before app-load deferral, reserved-type routing
// (app/shader/app_event/forget), and the user onMessage callback. Return
// true to continue routing; false to consume the message. Channelled
// messages never reach this filter.
sandbox.onMessageFilter([](const char* transport, const char* type, JsonDocument& doc) {
    if (strcmp(type, "my_platform_thing") == 0) { handleIt(doc); return false; }
    if (isDuplicate(doc["nonce"] | "")) return false;
    return true;
});

// Defer app/shader loads during a memory/CPU-sensitive window (e.g. voice
// recording — a Lua compile would stall the audio path). Applies to app/
// shader loads on the legacy path AND the "system" channel (handleSystemMessage
// checks the same defer flag). Stash is last-one-wins; clearing applies it
// immediately. Other message types flow normally. The applied stash routes
// straight to loading — the filter already ran at receipt (legacy path only),
// so a dedup filter like the one above won't drop it a second time.
sandbox.deferAppLoads(true);
// ... later ...
sandbox.deferAppLoads(false);          // applies any stashed app/shader now
sandbox.hasDeferredAppLoad();          // pending stash?

// Route a message through the sandbox's full receive pipeline exactly as if
// it arrived from a transport — channel-aware: a "channel" field in doc sends
// it through handleAppMessage/handleSystemMessage/onMessageWithChannel same
// as a real transport message; no "channel" field takes the legacy path
// (filter → deferral → reserved-type routing → onMessage). For wrappers with
// their own receive path, and for native tests.
sandbox.injectMessage("mqtt", "app", doc);
```

| Callback | Signature | Fires |
|----------|-----------|-------|
| `onTransportsWillConnect` | `void()` | Once, after Resident sets the default WS path and before transports start. Override the path here. |
| `onMessage` | `void(const char* transport, const char* type, JsonDocument&)` | **Legacy un-channelled path only** — fires for messages with no `channel` field, and only for non-reserved types (reserved types `app`/`shader`/`app_event`/`forget` are still routed internally on that path, no super-call needed). Channelled messages (`channel:"app"`/`"system"`/custom) never reach this callback — see [Channel routing](#channel-routing). |
| `onConnectionChange` | `void(Courier::State)` | On every state transition. Resident's internal handler updates `systemDisplay`/`systemLED` first; your callback runs alongside (does not replace). |
| `onConnected` | `void()` | When fully connected. Often used to load a bootstrap app — guard with a function-local `static bool loaded` to avoid re-firing on reconnect. |

### Channel routing

Incoming messages carry an envelope `channel` field that steers them onto one of three planes. This is the routing new senders should use; the un-channelled path above is legacy.

```json
{ "channel": "app",    "type": "turn", "data": { "n": 1 } }
{ "channel": "system", "type": "ota", "url": "..." }
{ "channel": "metrics", "type": "sample", "value": 42 }
```

| `channel` value | Routes to | Notes |
|-----------------|-----------|-------|
| `"app"` | `handleAppMessage` → Lua `on_event(ctx, event)` with `event.name = type` | Data plane. No reserved types here — `type:"forget"` on this channel is just an event, not a persistence op. Self-echo (`from == getDeviceId()`) and duplicate `nonce` (16-entry ring, exact match) are dropped before delivery. Gated like `sendAppEvent`: dropped when no app is loaded or the app defines no `on_event`. The legacy `app_event` envelope (`{"type":"app_event","name":...,"data":...}`) is still accepted here for one release, logging `[deprecated] app_event wrapper; send channel:"app" with type=<event name>` — until a [host hello](#hello) arrives, after which it is dropped and counted. |
| `"system"` | `handleSystemMessage` | Control plane. Reserved types are handled internally: `app` / `shader` (with `deferAppLoads` and the `description` display below), [`chunk`](#sandbox-controls), `forget`, [`framework`](#framework-modules), [`hello`](#hello), `goodbye`. Any other type falls through to the single `"system"` slot registered via `onMessageWithChannel("system", cb)`. |
| anything else | the matching slot registered via `onMessageWithChannel(name, cb)` | Single slot per channel name (exact string match), last registration wins. An unregistered channel is logged (`Resident::Sandbox: no handler for channel '<channel>' (type '<type>'); dropped`) and the message is dropped. Up to 8 slots (`onMessageWithChannel` does not include `"app"` — the data plane belongs to the Lua app, not a C++ slot). |
| *(absent)* | the legacy un-channelled path | Logs `[deprecated] un-channelled '<type>' message; sender should stamp channel`, then `onMessageFilter` → deferral → reserved-type routing → `onMessage`. Closed once a [host hello](#hello) arrives: dropped and counted instead. |

```cpp
// Public entry points, callable directly by wrappers with their own receive
// path (e.g. per-transport/topic hooks) — no loopback through Courier needed.
sandbox.handleAppMessage(transportName, type, doc);
sandbox.handleSystemMessage(transportName, type, doc);

// Register a "system" (or custom) channel slot.
sandbox.onMessageWithChannel("system", [](const char* transport, const char* type, JsonDocument& doc) {
    if (strcmp(type, "ota") == 0) { /* ... */ }
});
```

**Control-plane emit** — `sandbox.sendSystem(doc)` stamps `doc["channel"] = "system"` and sends it via the default transport (`courier().send`). For device control messages (voice start/end, etc.). Returns `false` when no network is configured or the send fails; the doc is stamped either way, so a caller inspecting it afterward always sees the envelope.

**Data-plane emit** — `sandbox.publishEvent(name, dataJson)` builds `{channel:"app", type:name, data, from:getDeviceId(), src:"device", seq, nonce, ts_ms}` and hands it to the event sink. `seq` is the device's per-sender monotonic sequence (uint32, per-boot, one counter for all frames sent via this path; the `nonce` suffix reuses the same count). It goes to: the sink set via `setEventSink(EventSink)` if one is registered, otherwise `courier().send` on the default transport. Rate-limited with a token bucket (5 events/s sustained, burst of 10). It never raises: `publishEvent` returns `false` only when the event will never go (no sink/network, send failure, oversize payload); a rate-limited or offline send is queued and returns `true`. `publishEventEx(name, dataJson, keep = false, channel = "app")` returns the underlying `SendResult::{Sent, Queued, Dropped}`. This is the shared implementation behind `events.send` (see [Lua API](#events-module)) and any C++ caller.

```cpp
using EventSink = std::function<bool(JsonDocument&)>;
sandbox.setEventSink([](JsonDocument& doc) {
    return myTransport.publish(doc);   // return true on success
});
```

**Description-on-load display** — when an `app`/`shader` load message (on either the legacy path or the `"system"` channel) carries a `description` field, it's shown on `systemDisplay` via `displayText()` at receipt (before any `deferAppLoads` stash is applied — a deferred load's description was already shown at receipt, so the deferred-apply path shows nothing). On by default; call `sandbox.setShowDescriptions(false)` to disable — e.g. on devices whose `systemDisplay` *is* the main app screen.

### Sandbox controls

```cpp
sandbox.loadApp(luaCode);              // compile and run a Lua source string
sandbox.loadShader(fields);            // generate Lua via ShaderTemplateFn, then loadApp
sandbox.loadChunk(luaChunk);           // run a chunk in the RUNNING app's state (surgical update)
sandbox.sendAppEvent(name, dataJson[, channel]); // queue an event to the running app (default channel "driver")
sandbox.onMessageWithChannel(name, cb); // register a channel slot (see Channel routing)
sandbox.sendSystem(doc);               // stamp channel:"system", send via default transport
sandbox.publishEvent(name, dataJson);  // stamp channel:"app" event, rate-limited/queued, send via sink
sandbox.publishEventEx(name, dataJson, keep, channel); // same, returning SendResult
sandbox.setEventSink(fn);              // override publishEvent's/events.send's destination
sandbox.setSystemSink(fn);             // override sendSystem's destination (hello + telemetry included)
sandbox.requestHello();                // re-queue the device hello
sandbox.hostHelloSeen();               // true once a host hello arrived this boot
sandbox.setShowDescriptions(show);     // toggle description → systemDisplay on app/shader load
sandbox.setTimezone("Europe/London");  // IANA zone — performs UDP lookup on first use
sandbox.hasTimezone();                 // true after a successful setTimezone call
sandbox.isAppRunning();                // true when an app is compiled and active
sandbox.suspendApp();                  // pause the running app's tick without unloading it
sandbox.resumeApp();                   // resume a suspended app
sandbox.isAppSuspended();              // true between suspendApp() and resumeApp()
sandbox.onSystemButtonHold(cb);        // cb(true) on hold (past threshold), cb(false) on release
sandbox.addOverlay(&ov, &surface, prio); // register a claim on a display surface, with priority
sandbox.requestOverlay(&ov, show);       // request show/hide; per-surface arbitration picks winners
sandbox.removeOverlay(&ov);              // deregister (tears the overlay down, resumes app if it suspended)
sandbox.startCapture(stream, format);  // {system, capture} bracket + mic stream
sandbox.endCapture();                  // stop the stream, close the bracket
sandbox.startMicStream();              // stream systemMic frames, no brackets (board owns control frames)
sandbox.stopMicStream();               // stop streaming
sandbox.isMicStreaming();              // true while streaming
sandbox.setMicStreamSink(fn);          // override the binary frame sink
sandbox.generationId();                // const String& — ID of the last loaded app/shader
sandbox.setTelemetryCallback(cb);      // wire telemetry JSON to your transport
sandbox.clearPersistedApp();           // wipe the saved app from the persistent store
```

`loadApp` stops any running app, calls `onAppReset()` on all extensions, generates a new `generationId`, and compiles the new app. An app must define at least one of `init`, `on_tick`, or `on_event` — compilation is rejected otherwise.

`loadShader` requires `SandboxConfig::shaderTemplate` to be set; it converts the `ShaderFields` map to Lua source, then calls `loadApp`.

`loadChunk(code)` runs a Lua chunk **in the running app's `lua_State`** — an in-place patch, where `loadApp` is a restart. Globals, queued events and timing survive, and `init()` is **not** re-called, so a chunk that reassigns a function or a table entry swaps it without costing the app its state. Wire entry: `channel:"system", type:"chunk", code:"..."`.

- If the chunk (re)defines `init`/`on_tick`/`on_event`, the cached dispatch refs are refreshed and the redefinition takes effect on the next dispatch.
- Success emits `chunk_applied` telemetry; a compile or runtime error emits `chunk_error` (Serial too) and leaves the app running. A chunk is not a transaction — statements before a runtime error did run.
- Returns `false` on compile error, runtime error, no app loaded, or during a `deferAppLoads` window. Deferred chunks are **dropped** with a log, never stashed.
- Never persisted. NVS keeps the base app; senders re-send chunks after a reboot.

`suspendApp` pauses the Lua tick (`on_tick` and event dispatch) without unloading the app — Courier and extension `update()` keep running. While suspended, drivers receive `onAppRunning(false)` so the status display is freed for direct text (e.g. a "Listening" overlay via `SystemDisplay::displayText()`); `resumeApp` reverses this with `onAppRunning(true)`. Both are no-ops when no app is loaded, and repeated calls don't re-notify. `isAppRunning()` stays `true` while suspended — suspension is a separate axis queried via `isAppSuspended()`. Events arriving while suspended are queued, not dropped (though a long suspend can overflow the 8-slot ring, losing the oldest), and `loadApp` always clears suspension.

`onSystemButtonHold(cb)` turns the `systemButton` role slot into a runtime hold gesture: `cb(true)` fires once when the button is held past the threshold (~500 ms), `cb(false)` on release. It is inert only during the boot countdown (where a hold forgets the persisted app), so it also fires in `Ready` with no app loaded. Combined with an [Overlay](#residentoverlay) and the [SystemMic](#residentsystemmic) streaming pump it composes into push-to-talk with no per-device boilerplate — see the `m5stick-voice` example.

`addOverlay` / `requestOverlay` / `removeOverlay` and `startMicStream` / `stopMicStream` / `isMicStreaming` / `setMicStreamSink` are covered under [Resident::Overlay](#residentoverlay) and [Resident::SystemMic](#residentsystemmic).

`setTimezone` is a no-op on `nullptr` or empty input. Success means ezTime resolved the zone (either from its own cache or via one UDP lookup to `timezoned.rop.nl`); failure logs and leaves `hasTimezone() == false`. Affects `ctx.localtime_h`, `ctx.localtime_m`, `time.hour()`, `time.minute()`, and `time.second()` in Lua.

### Identity and state accessors

```cpp
sandbox.getDeviceId();    // const String& — device ID derived from chip MAC
sandbox.getDeviceType();  // const char* — from SandboxConfig::deviceType
sandbox.isConnected();    // true when Courier reports State::Connected
sandbox.isTimeSynced();   // true after NTP/HTTP time sync
sandbox.hasNetwork();     // true iff cfg.network was set at construction
sandbox.courier();        // Courier::Client& — asserts if !hasNetwork()
sandbox.ws();             // Courier::WebSocketTransport& — asserts if !hasNetwork()
```

`courier()` and `ws()` are not nullable accessors — they assert. The pattern is *"if you wrote code that calls these, you also chose to set `cfg.network`"* — the static configuration choice should be obvious from the call site. Guard with `hasNetwork()` only in library code that intends to support both modes.

### Standalone mode

Omit `cfg.network` to run with no networking pulled in:

```cpp
Resident::SandboxConfig cfg;
cfg.extensions = {&myLED};
Resident::Sandbox sandbox{cfg};

void setup() {
    sandbox.setup();
    sandbox.loadApp(
        "function on_tick(ctx, dt_ms)\n"
        "  local t = ctx.time_ms / 1000\n"
        "  led.set_rgb(math.sin(t)*127+128, 0, 0)\n"
        "end\n"
    );
}

void loop() { sandbox.loop(); }
```

In standalone mode:

- `hasNetwork()` returns `false`; `courier()` and `ws()` assert.
- `isConnected()` always returns `false`.
- `onConfigureNetwork` / `onTransportsWillConnect` / `onMessage` / `onConnectionChange` / `onConnected` never fire — but registering them is harmless.

---

## Resident::Extension

The base class for all sandbox extensions. Extend it directly when you only need to register a Lua module — no hardware events required.

Include with:

```cpp
#include <ResidentExtension.h>   // or <Resident.h>
```

### Virtual interface

| Method | Default | Description |
|--------|---------|-------------|
| `name() const` | *(pure virtual)* | Module name as registered in Lua (e.g. `"imu"`) |
| `registerModule(LuaModule& m)` | no-op | Populate the Lua module table using the builder |
| `begin()` | no-op | Hardware / module init — called once by `Sandbox::setup()` |
| `update()` | no-op | Per-loop tick at full main-loop rate (not Lua's 10 FPS) |
| `onAppReset()` | no-op | Called before each new app is compiled |

### Idempotent early init

```cpp
Resident::Extension::beginExtension(myExtension);
```

Call this before `sandbox.setup()` to run `begin()` early (e.g. a status display that must be ready before the sandbox). `Sandbox::setup()` calls `beginExtension` on every extension; the second call is a no-op.

---

## Resident::Driver

Extends `Extension` with a hardware-event surface. Use `Driver` when your extension needs to fire events into the Lua `on_event` callback or react to app start/stop.

Include with:

```cpp
#include <ResidentDriver.h>
```

### Added interface

| Method | Default | Description |
|--------|---------|-------------|
| `onAppRunning(bool running)` | no-op | Called when an app starts (`true`) or stops (`false`) |

All `Extension` methods (`name`, `registerModule`, `begin`, `update`, `onAppReset`) are inherited unchanged.

### `sendEvent` (protected)

```cpp
void sendEvent(const char* name, const EventField* fields, int fieldCount);
```

Queues a driver event into the sandbox event ring. The event appears in Lua as `on_event(ctx, event)` with the fields as the `event.data` table — the same shape wire events use.

```cpp
// In a button driver's ISR or debounce handler:
EventField fields[] = {
    { "id",    EventField::INT,    { .i = buttonId } },
    { "state", EventField::STRING, { .s = "pressed" } },
    { "mag",   EventField::FLOAT,  { .f = 1.5f } },
    { "held",  EventField::BOOL,   { .b = true } },
};
sendEvent("button", fields, 4);
```

The event name `"button"` is special: it increments `ctx.trigger_count`.

### EventField struct

```cpp
struct EventField {
    const char* key;
    enum Type { INT, STRING, FLOAT, BOOL } type;
    union {
        int         i;
        const char* s;
        float       f;
        bool        b;
    };
};
```

| Field | Description |
|-------|-------------|
| `key` | Field name — appears as `event.data.<key>` in Lua |
| `type` | `EventField::INT`, `STRING`, `FLOAT`, or `BOOL` |
| `i` / `s` / `f` / `b` | The value, in the union member matching `type` |

### Inheritance ordering rule

When a Driver also implements another interface (e.g. `SystemDisplay`), `Driver` must be the **leftmost** base class:

```cpp
// OK — Driver (and therefore Extension) is leftmost
class MyDriver : public Resident::Driver, public Resident::SystemDisplay { ... };

// BROKEN — SystemDisplay is leftmost; the LuaModule trampoline cast will be wrong
class MyDriver : public Resident::SystemDisplay, public Resident::Driver { ... };
```

This matters because `LuaModule::method<>` casts the stored `Extension*` pointer directly to the `Class*` type. The cast is valid only when `Extension` is the leftmost base — i.e. when `static_cast<Class*>(extensionPtr)` produces the same address. See [Resident::LuaModule](#residentluamodule) for details.

### When to use Extension vs Driver

- Use `Extension` when you only register a Lua module (read sensors, control outputs from Lua, but no driver-generated events).
- Use `Driver` when your extension needs to push events into Lua (`sendEvent`) or respond to app start/stop (`onAppRunning`).

### Driver lifecycle and update cadence

Every object Resident manages — sandbox extensions and the device-role
peripherals (system display, system button, system LED, system mic) — is a `Driver`
(hence an `Extension`). `begin()` runs once for each at setup. `update()`
runs on a single de-duplicated list every loop:

- **Role peripherals** (whatever you assign to `config.systemDisplay` /
  `systemLED` / `systemButton`) update **every loop, always** — so
  the status screen and system button work before any app exists and across a
  brief reconnect.
- **Other extensions** update **only while an app is loaded** (running or
  suspended).
- **Connectivity gates neither** `update()`, nor the Lua `on_tick` (unless
  `SandboxConfig::gateTickOnConnection` is set).

A driver fills a device role by implementing the role interface (`SystemDisplay`
/ `SystemButton` / `SystemLED`, each a `Driver` subclass) — that's the
*capability*. (`SystemMic` is a role slot too, but not a `Driver`: it is a
[standalone capture interface](#residentsystemmic) whose lifecycle belongs to
the mic pump, not the extension set.) `SystemDisplay` additionally has an optional `restoreContent()`
(default no-op): repaint the surface's underlying content after the last
[Overlay](#residentoverlay) claiming it releases. Whether it's *used* in that role is the per-device config slot,
so the same driver is reusable across boards. An object fills at most one role.
(`StatusDisplay` / `StatusLED` remain as deprecated aliases for `SystemDisplay`
/ `SystemLED`.)

Driver events (`sendEvent`) are delivered to the app only while an app is
loaded; emitted with no app loaded, they are dropped.

---

## Resident::LuaModule

A builder that populates a Lua module table from C++ member functions, static functions, and constants. You receive a `LuaModule&` in your `registerModule` override — you do not construct one yourself.

Include with:

```cpp
#include <ResidentLuaModule.h>
```

### Member functions

```cpp
void registerModule(Resident::LuaModule& m) override {
    m.method<IMUDriver, &IMUDriver::accel>("accel")
     .method<IMUDriver, &IMUDriver::gyro>("gyro");
}
```

For const member functions:

```cpp
m.method<DisplayDriver, &DisplayDriver::width>("width")   // int width(lua_State*) const
```

The template requires two explicit parameters — the class type and the member function pointer. This is C++14-compatible; no extra compiler flags are needed.

### Static functions

```cpp
m.staticMethod("now_ms", [](lua_State* L) -> int {
    lua_pushinteger(L, millis());
    return 1;
});
```

`staticMethod` accepts any `lua_CFunction` (`int(*)(lua_State*)`).

### Fallthrough

```cpp
m.fallthrough(&MyModule::l_index);   // any lua_CFunction, receives (table, key)
```

Gives the module table a metatable whose `__index` is the supplied function — for extensions whose global must resolve missing keys against a table owned elsewhere. `ResidentLvglModule` uses this to keep luavgl's constants and constructors reachable alongside `lvgl.bind`.

### Constants

```cpp
m.constant("VERSION", 1)
 .constant("SCALE",   0.01)
 .constant("LABEL",   "imu")
 .constant("ENABLED", true);
```

Overloads accept `int`, `double`, `const char*`, and `bool`.

### The leftmost-base rule

`method<C, &C::fn>` stores your `Extension*` and casts it to `C*` at call time using `static_cast`. This is only correct when `Extension` is the leftmost base of `C` (so the pointer addresses are equal). Satisfy this by listing `Driver` (or `Extension`) first in any multi-inheritance class declaration. See [Inheritance ordering rule](#inheritance-ordering-rule) in the Driver section.

---

## Resident::Extensions

A fixed-capacity list of `Extension*` pointers passed to `SandboxConfig`.

```cpp
cfg.extensions = {&display, &button, &imu};
```

| Constant | Value | Description |
|----------|-------|-------------|
| `Extensions::MAX` | `12` | Maximum number of extensions per sandbox |

Extensions are stored in registration order. `begin()`, `registerModule()`, `update()`, and `onAppReset()` are all called in registration order.

The user owns the extension instances (typically global or static variables). The `Extensions` struct holds raw pointers and does not manage lifetime.

---

## Resident::RenderTargets

The render-target registry — the single place a board's drawable surfaces are declared, and the seam between "the panel, addressable" (the board's job) and the machinery a graphics library needs to draw on it (the module's job). It is INTERNAL machinery: the graphics modules resolve `bind(name)` against it and arbitrate ownership through it. It is deliberately NOT broadcast: surface geometry is an authoring fact, and the one authority for authoring facts is the document behind the hello's `profile` key. A static-capacity table (`RenderTargets::MAX` = 8), no allocation, board-lifetime (entries survive app loads; `clear()` exists for tests).

A board registers **one library-agnostic target per panel**:

```cpp
#include <ResidentRenderTargets.h>   // also pulled in by Resident.h

class MyPanel : public Resident::PanelTarget {
  int32_t width() const override  { return M5.Display.width(); }
  int32_t height() const override { return M5.Display.height(); }
  void blit(int32_t x, int32_t y, int32_t w, int32_t h,
            const uint16_t* px) override {
    M5.Display.startWrite();
    M5.Display.setAddrWindow(x, y, w, h);
    M5.Display.writePixels(px, (int32_t)w * h);
    M5.Display.endWrite();
  }
};
MyPanel myPanel;
Resident::RenderTargets::addPanel("main", &myPanel, "rect");
```

`PanelTarget` is the whole board-side surface: geometry plus a synchronous RGB565 blit. **Byte order: big-endian (high byte first)** — the SPI wire order, which is also LovyanGFX's internal 16-bit sprite format (`swap565`); a caller holding little-endian pixels (LVGL's native RGB565) swaps before calling, as `LvglModule`'s flush does.

The modules then build their own machinery over that one target when Lua binds it — `LgfxModule` allocates and owns a full-frame sprite and presents it with a single blit; `LvglModule` creates and owns the `lv_display_t`, its partial draw buffers and the flush callback over the same blit.

| Member | Description |
|--------|-------------|
| `addPanel(name, panel, shape = "rect")` | The board's registration: attach a `PanelTarget` and take geometry from it. Same name merges. Returns `false` when full or `panel` is null. |
| `add(name, w, h, shape, module)` | Declare a module bit on a target (what the modules call from `addDisplay`). Same name merges: geometry and shape refresh, module bit ORed in. `shape` `nullptr` keeps the existing/default. |
| `panel(name)` | The registered `PanelTarget*`, or null. |
| `claim(name, module)` | **Bind is the claim.** Set the owner (last claim wins). `false` for unknown names. |
| `owner(name)` / `isOwner(name, module)` | The owner bit (`0` = unowned); `isOwner` is the present-path gate and is strict — an UNOWNED target answers `false` for everyone. |
| `release(module)` | Release every target this module owns (called from each module's `onAppReset`); returns the count. |
| `releaseAll()` | Release everything, whoever owns it. |
| `count()` / `entry(i)` | Iterate the entries (`{name, w, h, shape, modules, owner, panel}`). |
| `clear()` | Tests only — surfaces are hardware, not app state. |

Names and shapes are stored as pointers; the registrant keeps them alive (string literals in firmware).

#### Bind is the claim (ownership)

Two graphics libraries cannot both drive one panel: a full-frame sprite push and LVGL's partial flushes overwrite each other and the glass strobes. So the registry holds **one owner bit per target**, and `lgfx.bind(name)` / `lvgl.bind(name)` are both the app's library DECLARATION and its ownership CLAIM:

- **Last bind wins**, either module, in either direction.
- The **non-owner's present path stands down silently**: an `lgfx` `flip()` is dropped, and LVGL's flush callback returns without touching the panel (its display refresh timer is paused too, so it doesn't even render). Drawing keeps working — only presenting is gated.
- **On takeover** the incoming owner repaints everything it can (`LvglModule` invalidates the whole active screen), because the pixels on the glass were the other library's a moment ago.
- **`onAppReset` releases every claim** (each module releases its own), so each app's first bind is a clean claim. This is also what kills the app-reset blank-frame race: the wipe of the outgoing LVGL tree invalidates a blank screen, but an unowned display never flushes it behind the incoming app.

Out of this arbitration by design: the **system/status display role** and **overlays**. Those write through the board's own path (the driver that owns the status text and its sprite), so connection text and overlays reach the glass whether or not an app has claimed the panel.

---

## Resident::SystemDisplay

Interface for connection-state text output. Implement it in a display driver and pass a pointer via `SandboxConfig::systemDisplay`. (Renamed from `StatusDisplay`, which remains as a deprecated plain alias — existing subclasses compile unchanged, but `SandboxConfig::statusDisplay` itself is deprecated in favor of `systemDisplay`.)

```cpp
class MyDisplay : public Resident::SystemDisplay {
public:
    void begin() override { /* init display hardware */ }
    void update() override { /* optional per-loop update */ }
    void displayText(const char* text) override {
        display.print(text);
    }
};
```

| Method | Default | Description |
|--------|---------|-------------|
| `displayText(const char* text)` | *(pure virtual)* | Show a status string — called by Resident's internal handler on connection state changes |
| `begin()` | no-op | Called once during `Sandbox::setup()` |
| `update()` | no-op | Called every `Sandbox::loop()` |

A Driver can implement `SystemDisplay` as a second interface for dual-use hardware (display + driver). Follow the [inheritance ordering rule](#inheritance-ordering-rule) — `Driver` must come first.

---

## Resident::SystemLED

Interface for a simple LED indicator driven by connection state. (Renamed from `StatusLED`, which remains as a deprecated plain alias — existing subclasses compile unchanged, but `SandboxConfig::statusLED` itself is deprecated in favor of `systemLED`.)

```cpp
class MyLED : public Resident::SystemLED {
public:
    void solidColor(uint32_t color) override {
        neopixel.setPixelColor(0, color);
        neopixel.show();
    }
};
```

| Method | Default | Description |
|--------|---------|-------------|
| `solidColor(uint32_t color)` | *(pure virtual)* | Set the LED to a packed `0xRRGGBB` color — called by Resident's internal handler on connection state changes |

Resident's internal handler calls `solidColor` automatically as the connection state changes (yellow during WiFi setup, cyan while transports connect, green when connected, orange while reconnecting, red on failure). `SystemLED` is a `Driver` subclass and therefore inherits the standard `Driver` lifecycle: `begin()` is called once during `Sandbox::setup()` and `update()` is called every loop (both default to no-ops). Override `begin()` to initialize LED hardware there rather than in the constructor.

---

## Resident::SystemMic

A capture source the runtime can stream: 16-bit signed mono PCM, with `sampleRate()` the single source of truth for the wire format. Assign one via `SandboxConfig::systemMic`.

Unlike the other role slots, **`SystemMic` is not a `Driver`** — it has no Lua surface, no events, no per-loop `update()`. It is a standalone interface whose lifecycle belongs to the **mic pump**: capture runs between `begin()` and `end()`, and the pump calls those in `startMicStream()` / `stopMicStream()`, so capture hardware (a shared codec, a background task) is held only while streaming. Any Lua-facing mic activity — levels, meters — is a separate extension's concern, fed by your board's own plumbing.

### Using an M5 device? The driver is already written

Resident ships `Resident::M5Mic` (any board M5Unified's mic supports: M5StickS3, M5StickC Plus2, M5Stack ...). It is opt-in — one include, one object, nothing else to get right:

```cpp
#include <ResidentM5Mic.h>       // not pulled in by Resident.h

Resident::M5Mic micDriver;
cfg.systemMic = &micDriver;
```

M5Unified must be in *your* project's `lib_deps` (it already is if you build for an M5 board); Resident itself does not depend on it. Internally `M5Mic` handles the awkward parts of M5Unified's asynchronous mic — buffer-completion tracking, keeping the capture queue primed, mic-task priority — and exposes `audit()` health counters if you want to check pipeline integrity in the field. See `src/ResidentM5Mic.h` for the full story; it is also the worked reference for writing your own adaptor.

### The interface

```cpp
class MyMic : public Resident::SystemMic {
public:
    bool begin() override { return startCapture(); }  // acquire hw; pipeline runs from here on
    void end() override { stopCapture(); }            // release hw (e.g. shared codec)
    int read(int16_t* buf, int maxSamples, int timeoutMs) override {
        // Copy out of a buffer *we* own; return what was actually ready.
        return drainCaptured(buf, maxSamples, timeoutMs);
    }
    uint32_t sampleRate() const override { return 16000; }
};
```

| Method | Default | Description |
|--------|---------|-------------|
| `begin()` | *(pure virtual)* | Acquire the capture hardware and start the pipeline; `false` = failed. Re-`begin()` while capturing must be benign |
| `end()` | no-op | Stop capture and release the hardware, e.g. for the speaker side of a shared full-duplex codec |
| `read(int16_t* buf, int maxSamples, int timeoutMs)` | *(pure virtual)* | Drain up to `maxSamples` 16-bit mono samples; return the count actually written (0 if none ready) |
| `sampleRate()` | *(pure virtual)* | Capture rate in Hz (e.g. `16000`) |
| `frameSamples()` | `512` | Natural read chunk size (the pump's per-loop read size, capped at 512) |

The **streaming pump** ships its frames over the WebSocket:

```cpp
bool ok = sandbox.startMicStream();  // mic->begin(), then each loop(): drain + send raw int16 frames
sandbox.stopMicStream();             // mic->end() — capture hardware released
sandbox.isMicStreaming();            // true while streaming
sandbox.setMicStreamSink(fn);        // bool(const uint8_t* data, size_t len) — default: ws().sendBinary
```

`startMicStream()` returns `false` (and does not start streaming) when no `systemMic` is configured or its `begin()` fails. While streaming, each `Sandbox::loop()` reads up to `frameSamples()` (capped at an internal 512-sample buffer) and forwards the bytes to the sink — the default sink is `ws().sendBinary`. The pump adds **no framing or control frames**. Streaming is independent of app state — it continues while the app is suspended.

### Capture brackets

`startCapture` / `endCapture` wrap the same pump in control-plane frames, so the metadata rides the bracket and the media payloads stay raw:

```cpp
sandbox.startCapture();            // stream = 1, format = 1 by default
sandbox.endCapture();
```

```json
{ "channel": "system", "type": "capture", "data": { "state": "start", "stream": 1, "format": 1 } }
{ "channel": "system", "type": "capture", "data": { "state": "end",   "stream": 1 } }
```

The start bracket is sent before the first media frame; if it fails to send, capture does not start. A failed `startMicStream()` closes the bracket it opened and `startCapture` returns `false`. `startCapture` on an already-streaming sandbox returns `true` without re-bracketing. Boards that own their own control frames use `startMicStream` / `stopMicStream` directly instead.

### Writing a mic adaptor

Capture backends are usually asynchronous — DMA or a background task filling buffers while your code runs — and that makes drivers easy to get subtly wrong. Every rule below has silently corrupted audio in a real driver:

1. **Capture runs from `begin()` to `end()`; `read()` only drains.** Keep the pipeline running and its buffers queued from `begin()` onwards; `read()` copies out what already landed. Don't start a capture inside `read()` and wait for that one buffer — a pipeline that holds a destination only while `read()` is in flight loses everything in between. Backends commonly discard the partial chunk they were holding whenever their queue runs dry, with no error and no counter, and every gap is an audible splice.
2. **Never give capture hardware the caller's `buf`.** Record into memory the driver owns and copy out. An async backend keeps writing its destination after the call that queued it returned — so passing `buf` straight through is a background write into memory the caller already considers its own.
3. **`timeoutMs == 0` means do not block.** Return what is ready this instant, `0` if that's nothing. The pump calls `read(buf, frameSamples(), 0)` from `Sandbox::loop()`, so blocking there stalls the whole sandbox — app ticks, transports, overlays and all. `timeoutMs > 0` is a ceiling to respect, not a duration to fill; never block unbounded.
4. **Short reads are legal.** The return value is authoritative — never assume `maxSamples` were written. Returning `0` is routine and just means nothing has been captured yet.
5. **Assume a single caller.** `read()` is called by exactly one reader — the pump, or one producer task — so drivers need no locking against concurrent reads.

Backend-specific extras (health counters, per-channel metrics, one-shot capture) belong on your concrete driver type, reached by the board code that owns the instance — not on `SystemMic`. `src/ResidentM5Mic.h` is the worked reference over an asynchronous backend, including how it derives buffer completion without polling.

---

## Resident::Overlay

An overlay is a transient **claim on a display surface** — a "Listening" prompt, a recording animation — shown on top of, or instead of, whatever the surface normally displays.

```cpp
class ListeningOverlay : public Resident::Overlay {
public:
    void onAcquire() override { display.displayText("Listening"); }
    // onDraw(dt) / onRelease() are optional too (default no-op)
};
```

| Method | Default | Description |
|--------|---------|-------------|
| `onAcquire()` | no-op | You now own the surface; paint the first frame here for immediate response |
| `onDraw(dtMs)` | no-op | Animation heartbeat while owning, paced on the app-tick cadence (~10 FPS) — the overlay takes over the suspended app's tick slot. Event-driven repaints outside `onDraw` are fine too |
| `onRelease()` | no-op | You no longer own the surface — wind down internal state only; the arbiter handles app resume and surface restore |

Register and drive overlays from the Sandbox:

```cpp
sandbox.addOverlay(&overlay, &systemDisplay, 100);  // claim on a surface, with priority
sandbox.requestOverlay(&overlay, true);             // request show; false to hide
sandbox.removeOverlay(&overlay);                    // deregister
```

**Arbitration is per surface.** Overlays bound to the same surface contend by priority (highest wins; ties go to the earlier registration); overlays on different surfaces are independent and can all show at once. A `nullptr` surface means a **dedicated surface**: the overlay contends with nothing, never suspends the app, and no restore is issued for it — right for a display that exists only to show this overlay.

**App suspension is derived, not declared:** the app is suspended while any winning claim's surface is *dual-role* — the same display is both an app-facing extension (in `cfg.extensions[]`) **and** a `system*` role slot. On such a shared surface the app must be stopped from drawing over the overlay, so the arbiter calls `suspendApp()` while the claim is held and `resumeApp()` when it ends. An app pushed while a dual-role claim is held loads suspended rather than ticking beneath the overlay. The arbiter only resumes an app it suspended itself — a device-initiated `suspendApp()` survives overlay churn.

**Surface restore belongs to the surface.** When a surface's last claim releases, the arbiter calls `SystemDisplay::restoreContent()` on it — repaint the underlying content (last app frame, idle screen, prior status text) there. Overlays never restore; a handoff to another overlay on the same surface issues no restore.

Core ships the overlay *mechanism* and no concrete overlays; the running app is not overlaid by any of Resident's own status screens (those show only when no app owns the display).

---

## Lua API

Apps run inside the sandbox Lua state. The sandbox provides built-in globals and modules; drivers add their own globals via `registerModule`.

### App callbacks

At least one of these globals must be defined in the loaded Lua source. If none are found, the app is rejected.

```lua
function init(ctx)
    -- called once after compilation; use for one-time setup
end

function on_tick(ctx, dt_ms)
    -- called at 10 FPS; dt_ms is elapsed ms since the last tick
end

function on_event(ctx, event)
    -- called for each queued event (driver events and app_events)
end
```

All callbacks receive a `ctx` table. `on_tick` also receives `dt_ms` (integer, milliseconds). `on_event` also receives an `event` table.

### `ctx` table

| Field | Type | Description |
|-------|------|-------------|
| `time_ms` | integer | Milliseconds since the current app was loaded |
| `trigger_count` | integer | Number of `"button"` driver events since BOOT (not reset by app loads; kept for shader compatibility) |
| `generation_id` | string? | The `generationId` the server stamped on the app load message — `nil` when the load didn't carry one (direct C++ loads, NVS restores) |
| `utc_h` | integer | Current UTC hour (0–23) |
| `utc_m` | integer | Current UTC minute (0–59) |
| `localtime_h` | integer | Local hour — equals `utc_h` unless a timezone has been set |
| `localtime_m` | integer | Local minute — equals `utc_m` unless a timezone has been set |

The table is IDENTICAL in every callback. `localtime_h` / `localtime_m` reflect local time only after `Sandbox::setTimezone` succeeds; otherwise they equal `utc_h` / `utc_m`.

### `event` table

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Event name (e.g. `"button"`, `"my_event"`) |
| `from` | string | Source identifier — empty string for driver events |
| `ts_ms` | integer | Timestamp in milliseconds (`millis()`) when the event was queued |
| `channel` | string | Source discriminator, always present: `"app"` or `"runtime"` for wire-borne frames (both delivered here), `"driver"` for hardware/driver events and host-firmware `sendAppEvent` injections (unless the caller passed a channel) |
| `src` | string? | The frame's `src` envelope field (e.g. `"server"`), only when present on the frame; `nil` for internal events |
| `seq` | integer? | The frame's per-sender monotonic `seq`, only when present on the frame; `nil` for internal events |
| `data` | table | The payload, for EVERY event — driver and wire alike, one shape. Parsed by one set of rules: strings (unescaped), integers, floats, booleans, and nested objects/arrays to 3 container levels (deeper containers are skipped with their key; JSON `null` leaves a hole at its array index). Unparseable or oversized (> `RESIDENT_EVENT_JSON_MAX`) data drops the whole event rather than delivering it garbled |
| *(deprecated shadow)* | scalar | DRIVER events also mirror their top-level scalars onto the event table (`event.index`). Envelope keys win over a colliding field. Read `event.data.*`; the shadow goes with the next major |

```lua
function on_event(ctx, event)
    if event.name == "button" then
        log.info("button " .. tostring(event.data.id))
    elseif event.name == "update" then
        log.info("color: " .. event.data.color)
    end
end
```

### `log` module

Writes to the device serial port. `log.error` also emits a `log_error` telemetry event.

| Function | Description |
|----------|-------------|
| `log.info(msg)` | Print info message |
| `log.warn(msg)` | Print warning message |
| `log.error(msg)` | Print error message and emit telemetry |

```lua
log.info("hello from Lua")
log.error("something went wrong")
```

### `events` module

Publishes an event on the app data plane (`channel:"app"`) — the Lua-side entry point to [`Sandbox::publishEvent`](#channel-routing), which every recipient (this device's own network peers, or a server relay) delivers back as `on_event(ctx, event)` with `event.name` set to the published name.

| Function | Returns | Description |
|----------|---------|-------------|
| `events.send(name)` | `"sent"` \| `"queued"` \| `"dropped"` | Publish `name` with no data |
| `events.send(name, data)` | same | Publish `name` with a table of string/number/boolean/table values as `event.data` |
| `events.send(name, data, opts)` | same | `opts.keep = true` marks a message queue-overflow eviction never removes |

Both success states are truthy, so a caller that only needs "will this go?" can treat the result as a boolean.

```lua
events.send("turn")
events.send("color_change", { hue = 180, label = "warm" })
events.send("state", { on = true, pos = { x = 1, y = 2 }, tags = { "a", "b" } })
events.send("escalation", { level = 3 }, { keep = true })
```

`data` serializes to a JSON object. String keys and values are JSON-escaped (`"`, `\`, and control characters). Values may be strings, numbers, booleans, or tables nested up to 3 table levels (counting `data` itself): a string-keyed table becomes a JSON object; a table with a non-empty array part (`#t > 0`) becomes a JSON array of elements `1..#t` (don't mix array and string keys — string keys of an array-part table are ignored). Top-level integer keys, deeper tables, and other value types (functions, userdata) are silently skipped per-key; unsupported array *elements* serialize as `null` to hold their position.

Serialized into a bounded buffer of `RESIDENT_EVENT_JSON_MAX` bytes (default 1024; override with a build flag, e.g. `-DRESIDENT_EVENT_JSON_MAX=2048`). An oversized payload is never truncated — the event is dropped and a line is logged to Serial.

**Delivery.** Sends over the rate limit (shared token bucket: 5 events/s sustained, burst of 10) or while offline QUEUE into a bounded outbound queue (`RESIDENT_EVENT_QUEUE_SIZE`, default 16) and drain in order from `loop()` as tokens and connectivity allow. Envelopes are stamped (`seq`/`nonce`) at enqueue, so ordering holds and retries are dedup-safe. Overflow eviction prefers the oldest non-keeper and feeds the drop counter. `"dropped"` means the event will never go: empty name, oversize payload, or evicted while the queue was full of keepers.

### `lgfx` module (optional)

Idiomatic LovyanGFX drawing from Lua — present only when the firmware registers displays into an `LgfxModule` and lists it in `SandboxConfig::extensions`:

```cpp
#include <ResidentLgfxModule.h>
M5Canvas canvas{&M5.Display};                          // the frame buffer object
Resident::LgfxSpriteTarget<M5Canvas> lgfxMain{&canvas};
Resident::LgfxModule lgfxModule;
// setup: Resident::RenderTargets::addPanel("main", &myPanel);
//        lgfxModule.addDisplay("main", &lgfxMain);
//        cfg.extensions = {..., &lgfxModule};
```

Both adapters are duck-typed templates — instantiate with anything carrying LovyanGFX's drawing API (`LGFX_Device`, `LGFX_Sprite`, `M5Canvas`, `M5GFX`, a test fake). With `LgfxSpriteTarget<T>` the MODULE owns the frame buffer: on the first bind it calls `setColorDepth(16)` + `createSprite(...)` at the registered panel's geometry (idempotent — a board that pre-created the sprite for its own status path keeps the one buffer), and `flip()` blits it through the panel. `LgfxLovyanTarget<T>` is for targets that present themselves: construct it with a presenter callback, or with none for direct-to-panel targets where drawing is already live. Colors are 24-bit `0xRRGGBB` everywhere; LovyanGFX converts to the panel/sprite depth (it interprets `uint32_t` colors as RGB888).

```lua
local g = lgfx.bind("main")      -- raises a Lua error for unknown names
g:fillScreen(0x000000)
g:fillCircle(80, 60, 20, 0xFF5533)
g:setTextColor(0xFFFFFF); g:setTextSize(2); g:setCursor(4, 4); g:print("hi")
g:drawString("centered", g:width() // 2, 30)
g:flip()                         -- present to the glass
```

Handle methods (colon-call), matching LovyanGFX names/argument orders: `fillScreen(c)` · `drawPixel(x,y,c)` · `drawLine(x0,y0,x1,y1,c)` · `drawRect/fillRect(x,y,w,h,c)` · `drawRoundRect/fillRoundRect(x,y,w,h,r,c)` · `drawCircle/fillCircle(x,y,r,c)` · `drawTriangle/fillTriangle(x0,y0,x1,y1,x2,y2,c)` · `setTextColor(fg[,bg])` · `setTextSize(s)` · `setTextDatum(d)` (constants on the module table: `lgfx.TL_DATUM`, `TC`, `TR`, `ML`, `MC`, `MR`, `BL`, `BC`, `BR`, `L_BASELINE`, `C_BASELINE`, `R_BASELINE`) · `setCursor(x,y)` · `print(text)` · `drawString(text,x,y)` · `width()` · `height()` · `flip()`.

**Present semantics:** `g:flip()` presents the frame — one blit of the module-owned sprite through the target's registered [`PanelTarget`](#residentrendertargets), or (for a target with no readable buffer) the presenter callback the firmware supplied, or nothing at all for direct-to-panel targets where drawing is live. Deliberately small this pass: default font + size multiplier only — no font selection, images, or Lua-created sprites.

**`lgfx.bind(name)` is also the ownership claim** ([bind is the claim](#bind-is-the-claim-ownership)): it allocates the frame buffer if needed and takes the target. While another module owns it — because the app called `lvgl.bind` on the same target, or because an app reset released every claim — `g:flip()` is **dropped silently**; drawing calls still work. `addDisplay(name, target, shape = "rect")` declares the `MODULE_LGFX` bit on the target in the [`RenderTargets`](#residentrendertargets) registry.

### `lvgl` module (optional)

Retained-mode UI from Lua — LVGL 9 through the [Inanimate luavgl fork](https://github.com/inanimate-tech/luavgl)'s display-scoped binding. Present only when the firmware names registered panel targets in an `LvglModule` and lists it in `SandboxConfig::extensions`. Opt-in like the lgfx module, with an extra requirement: the board's own build must supply LVGL and luavgl (`lib_deps`). There is **no glue driver to write**: the module owns `lv_init`, the `millis` tick source, the `lv_display_t` over the board's [`PanelTarget`](#residentrendertargets), its draw buffers, the flush callback, the `lv_timer_handler` pump and the app-reset tree wipe. See `examples/m5stick-demo`'s `m5stick-lvgl` env for the full wiring.

```cpp
#include <ResidentLvglModule.h>   // not pulled in by Resident.h
Resident::LvglModule lvglModule;
// setup, after the board registered its panel:
Resident::RenderTargets::addPanel("main", &myPanel);
Resident::LvglModule::DisplayOptions opts;
opts.dpi = 240;                                 // 0 = LVGL's default
opts.bufferRows = 14;                           // 0 = auto (height/10, min 8)
lvglModule.addDisplay("main", opts);            // or addDisplay("main")
// cfg.extensions = {..., &lvglModule};
```

`DisplayOptions` also takes a board-supplied draw buffer (`buffer` + `bufferBytes`, e.g. `heap_caps_malloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)`); leave it null and the module allocates with `lv_malloc`. The `lv_display_t` is created **on the first bind**, so a board whose apps never use LVGL allocates no display and no buffers. `LV_COLOR_DEPTH` must be 16 (the blit contract is RGB565); the flush byte-swaps LVGL's little-endian pixels into the panel's wire order.

```lua
local h = lvgl.bind("main")    -- raises a Lua error for unknown names
h.Label { text = "hi", align = lvgl.ALIGN.CENTER }
h:set_theme { screen = { bg_color = "#0b0b10" } }
```

`lvgl.bind(name)` returns luavgl's display-scoped handle (idempotent per display): widget constructors parented to that display's active screen, `screen()`/`clean()`/`HOR_RES()`/`VER_RES()`/`mirror()`/`set_default()`/`set_theme{...}`, everything else falling through to the full luavgl module — see the fork's `docs/display-bind.md` for the handle contract and `prompts/lvgl.md` for the app-author surface. The `lvgl` global is Resident's module table (just `bind`), with a metatable falling through to luavgl's own module table — so `lvgl.Font`, `lvgl.ALIGN`, `lvgl.Anim` etc. resolve normally once the module has loaded, which happens on the first `bind` call. Two things follow: fallthrough keys are `nil` before any `bind` (bind first — apps always do), and Resident's name-based `bind` shadows luavgl's userdata-based `lvgl.bind(disp)` (use `lvgl.disp` functions if you truly need the raw form).

**`lvgl.bind(name)` is also the ownership claim** ([bind is the claim](#bind-is-the-claim-ownership)): it creates the display if needed, takes the target from whoever held it, and invalidates the whole active screen so the takeover repaints every pixel the other library left behind. While this module does NOT own the target, the flush callback returns without touching the panel and the display's refresh timer is paused — that is how a wiped, unowned tree can no longer race a blank frame onto the glass behind the incoming app. `onAppReset` wipes the tree (`lv_obj_clean` — luavgl invalidates Lua handles on C-side deletion) and releases the claim. Handle-level caching (one handle per display) stays luavgl's business. Themes are Lua's business: the module bakes none, and apps install their own via `h:set_theme{...}`.

### `store` module

An app-scoped **persistent** KV slot of scalars — state here survives `loadApp` (same identity) and reboot. RAM-backed with debounced write-through to the persistent store (NVS on device): at most one write per ~2 s of mutation quiet, plus a flush at least every 30 s while dirty (so an app that mutates on every tick still persists), on app unload, and at capture start.

| Function | Returns | Description |
|----------|---------|-------------|
| `store.get(key)` | string \| number \| boolean \| nil | Value for `key`, `nil` if absent |
| `store.set(key, value)` | boolean | Set a scalar (string/number/boolean). `nil` deletes. `false` if the value is non-scalar or the write would exceed the budget (rejected whole — no partial writes) |
| `store.keys()` | array | All keys currently in the slot |
| `store.clear()` | — | Empty the slot |
| `store.remaining()` | integer | Bytes left in the persisted budget (0 when at/over) |

**Scoping / reset policy:** the slot is namespaced by an app identity the server provides on the load message — `{channel:"system", type:"app", code:"...", storeNs:"<id ≤32 chars>"}`. Loading with the **same** `storeNs` preserves the slot; a **different** `storeNs` clears it first (persisted immediately); missing `storeNs` uses the shared default namespace `"app"`. The namespace is persisted alongside the data, so the policy holds across reboots. Direct C++ `loadApp()` calls leave the namespace unchanged.

**Budget:** total persisted size (namespace + keys + values, serialized) is capped at `RESIDENT_STORE_JSON_MAX` (default 2048 bytes, build-flag overridable).

### `time` module

Reads wall-clock time from NTP (via ezTime). Functions return UTC unless a timezone is set via `Sandbox::setTimezone`.

| Function | Returns | Description |
|----------|---------|-------------|
| `time.is_valid()` | boolean | `true` if NTP time has been acquired |
| `time.hour()` | integer | Current hour (0–23), local if timezone set |
| `time.minute()` | integer | Current minute (0–59), local if timezone set |
| `time.second()` | integer | Current second (0–59), local if timezone set |
| `time.day_id()` | integer | Days since device boot (`millis() / 86400000`) — useful as a cache key for daily state |
| `time.has_timezone()` | boolean | `true` if `setTimezone` succeeded |

```lua
function on_tick(ctx, dt_ms)
    if time.is_valid() then
        local h = time.hour()
        local m = time.minute()
        -- display h:m
    end
end
```

### `surfaces` module

The board's render targets ([`RenderTargets`](#residentrendertargets)), readable from Lua. Always present: a board that registers no panel lists nothing, which saves every consumer a capability check.

| Function | Returns | Description |
|----------|---------|-------------|
| `surfaces.list()` | array | Every registered surface, in registration order |
| `surfaces.get(name)` | table or nil | One surface by name; `nil` when the board has no such surface |

Each entry is `{ name, w, h, shape }`, with `shape` `"rect"` or `"round"`. The names are the ones `lgfx.bind(name)` and `lvgl.bind(name)` take.

Geometry is read from the panel at call time; the registry caches nothing for a panel-backed target. This is deliberate: a board registers its panels in a config function that commonly runs during **static init**, before `M5.begin()` and before any display driver's `begin()`, so asking a panel its size there dereferences hardware that does not exist yet. `addPanel` therefore never asks, and every reader gets live numbers.

Why it exists: a consumer that needs to know what surfaces a board has can ASK the device. A framework module that would otherwise be handed the geometry out of band (a per-board configuration file, a profile layer) can read it instead, and what is read from the hardware cannot disagree with the hardware.

```lua
local m = surfaces.get("main")
if m and m.shape == "round" then
    -- compose in rings; the corners are not glass
end
```

### Shader-compatible globals

These functions are always in scope — they are designed for use in shader expressions as well as full apps.

| Function | Returns | Description |
|----------|---------|-------------|
| `rgb(r, g, b)` | integer | Pack normalized floats (0–1) into a color value. Returns a **negative** packed int; the convention is that a negative return from a shader function signals "this is a color". |
| `fract(x)` | number | Fractional part: `x - floor(x)` |
| `beat(bpm, t)` | number | `t / (60000 / bpm)` — beat phase in beats; `fract(beat(120, ctx.time_ms))` gives a 0–1 sawtooth at 120 BPM |
| `noise2d(x, y)` | number | Deterministic 2D value noise, returns `-1` to `+1` |

Bare math functions are also registered as globals (so shader expressions don't need the `math.` prefix):

| Global | Equivalent |
|--------|-----------|
| `floor(x)` | `math.floor(x)` |
| `ceil(x)` | `math.ceil(x)` |
| `abs(x)` | `math.abs(x)` |
| `sin(x)` | `math.sin(x)` |
| `cos(x)` | `math.cos(x)` |
| `tan(x)` | `math.tan(x)` |
| `sqrt(x)` | `math.sqrt(x)` |
| `min(a, b)` | `math.min(a, b)` |
| `max(a, b)` | `math.max(a, b)` |
| `fmod(a, b)` | `math.fmod(a, b)` |

### Driver-provided modules

Each extension is registered as a global table named by `Extension::name()`. For example, a driver returning `"imu"` from `name()` makes `imu.accel()` available:

```lua
function on_tick(ctx, dt_ms)
    local ax, ay, az = imu.accel()
    log.info("ax=" .. tostring(ax))
end
```

See [Writing a Driver](#writing-a-driver) for the C++ side of this.

---

## Message Protocol

This section describes the reserved types on the legacy un-channelled path (no `channel` field) — see [Channel routing](#channel-routing) for the full envelope picture, including the `"app"` data plane and custom channels. Resident routes four JSON message types (`app`, `shader`, `app_event`, `forget`) internally on this path — they never reach the user's `onMessage(cb)` callback. Any other type is forwarded to `onMessage` if registered. The `"system"` channel handles `app`/`shader`/`forget` identically (same `loadApp`/`loadShader`/`clearPersistedApp` calls, same deferral and description-display behavior), adds `chunk`/`framework`/`hello`/`goodbye`, and has no `app_event` — the app data plane is `channel:"app"`.

### `app` — load a Lua app

```json
{ "type": "app", "code": "function on_tick(ctx, dt_ms) ... end" }
```

Calls `Sandbox::loadApp(doc["code"])`. Any previously running app is stopped first.

### `shader` — load a shader expression

```json
{ "type": "shader", "expr": "rgb(fract(ctx.time_ms / 2000.0), 0, 0)" }
```

The entire JSON document (as a `ShaderFields` map of string key/value pairs) is passed to `SandboxConfig::shaderTemplate`, which must return valid Lua source. The result is passed to `loadApp`. Requires `shaderTemplate` to be set.

### `app_event` — send an event to the running app

```json
{ "type": "app_event", "name": "color_change", "data": { "hue": 180 } }
```

Calls `Sandbox::sendAppEvent(name, dataJson)`. The event arrives in Lua as `on_event(ctx, event)` with `event.data` set to the parsed `data` object.

### `forget` — clear the persisted app

```json
{ "type": "forget" }
```

Calls `Sandbox::clearPersistedApp()`. The next boot will not restore any app. Equivalent to calling `clearPersistedApp()` directly.

### App persistence

The last app (or shader) that loads successfully — compiles **and** runs `init()` without error — is saved to flash (NVS) and auto-reloaded on the next boot.

Once the device is reachable — **connected**, or immediately in standalone mode — if a saved app exists the status display shows the device identity with a 20-second countdown before loading it (while connecting, the usual connection-status text shows instead). A networked device that never connects stays on the connection screen and does not auto-load.

```
Device ID: <deviceId>
Type: <deviceType>

20s
```

You need the device ID to push apps to the device, so the countdown is a reminder. It is a timer (not press-to-continue) because not every board has a button. An app/shader arriving over the network also ends the countdown (it loads the incoming app). If a `SystemButton` is configured, then during the countdown a **tap** loads the saved app immediately and a **long press** (≥1s) forgets it — the device then settles on the ready screen with nothing to restore.

When **no** app is loaded — a fresh device, or after a load fails — the status display rests on the same identity screen without the countdown line:

```
Device ID: <deviceId>
Type: <deviceType>
```

This "ready" screen appears once the device is reachable (connected, or immediately in standalone mode); while connecting, the usual connection-status text shows instead, and while an app runs the app owns the screen.

If a saved app fails to load — for example after the firmware was reflashed with a changed runtime surface — it is discarded and the device falls back to the ready screen (telemetry `persist_load_failed`).

Config fields related to persistence:

- `persistApps` (default `true`) — turn persistence off for a build.
- `systemButton` (`Resident::SystemButton*`, default `nullptr`) — a button the runtime polls to skip the countdown.
- `persistentStore` (`Resident::PersistentStore*`, default `nullptr`) — override the backing store; `nullptr` uses NVS on device.

Send `{"type":"forget"}` (or call `clearPersistedApp()`) to wipe the saved app.

### Telemetry (outgoing)

Every telemetry emission goes out TWO ways:

1. **On the wire, by default**: a channelled control-plane frame, queued and
   drained from `loop()` (emissions often happen in the receive context, where
   a direct send would be a reentrant WS write):

```json
{ "channel": "system", "type": "telemetry",
  "data": { "name": "compile_error", "generationId": "1a2b3c", "error": "..." } }
```

   The queue is a bounded ring (8 slots, 7 usable); while unsendable
   (offline), overflow drops the oldest. Sends route through the system sink
   when one is set (`setSystemSink` — the control-plane mirror of
   `setEventSink`), else the transport.

2. **Via `TelemetryCallback`**, when one is set — a flat format, for boards
   that route telemetry themselves:

```json
{ "type": "telemetry", "generationId": "1a2b3c", "name": "app_compiled", "data": {} }
{ "type": "telemetry", "generationId": "1a2b3c", "name": "runtime_error", "data": { "error": "..." } }
```

| Telemetry name | Trigger |
|----------------|---------|
| `app_received` | `loadApp` or `loadShader` called |
| `app_compiled` | App compiled successfully |
| `compile_error` | Compilation or execution failed; `data.error` contains the message |
| `runtime_error` | A Lua callback threw an error. `on_tick` errors are rate-limited (see [Limits](#limits)); `init` and `on_event` errors are emitted immediately. |
| `log_error` | App called `log.error(msg)` |
| `app_restored` | A persisted app was successfully restored on boot |
| `persist_load_failed` | A persisted app failed to load on boot and was discarded |
| `persist_too_big` | An app was too large to save to the persistent store |
| `store_full` | An over-budget `store.set` was rejected; `data.error` carries the key (once per key per app load) |
| `framework_applied` | A framework (built-in or slot update) loaded successfully |
| `framework_error` | A framework chunk failed to compile or run; `data.error` carries the message. A failing slot blob is discarded and the built-in runs |
| `dropped` | The drop counter's periodic report; `data.count` = items silently dropped since boot (ring overflow, oversize payloads, rate limits, closed legacy paths). At most one report per minute, only when changed |

The wire path needs no wiring; the callback is there for boards that want an additional sink.

### Hello

On every transport connect the sandbox queues a device hello — the device announcing itself so the host never has to assume its shape — drained by `loop()` (never sent from the connect context):

```json
{ "channel": "system", "type": "hello", "data": {
  "protocol": 1,
  "deviceType": "m5stick", "firmware": "1.4.0", "bootId": "9f2c11a0",
  "profile": "m5stick@2",
  "limits": { "eventBytes": 1024, "replyBytes": 1024, "storeBytes": 2048,
              "storeNsChars": 32, "eventsPerSec": 5 },
  "app": { "storeNs": "oracle", "generationId": "g1" }
} }
```

- `protocol` is `RESIDENT_PROTOCOL_VERSION` — the whole compatibility story. There is deliberately no feature list: chunk support is part of protocol version 1, telemetry needs no advance notice, and capture announces itself via its bracket. A genuinely optional capability introduces its own hello field when it exists.
- `firmware` / `profile` come from `SandboxConfig::firmwareVersion` / `profileRef` (omitted when unset).
- `limits` are the build's actual constants — hosts should size payloads against them instead of assuming.
- Deliberately absent: surfaces, sensors, driver modules — authoring facts, whose one authority is the document behind `profile`. The hello carries only what a host needs to operate the session.
- `app` describes what is running (or persisted and awaiting the boot countdown): its store namespace, plus `generationId` only when the wire stamped one (a restored app's self-generated id is omitted).
- `sandbox.requestHello()` re-queues it at any time.

A host hello may answer on the same channel: `{ "channel":"system", "type":"hello", "data": { "protocol":1, "tz":"Europe/London" } }`. The sandbox applies `tz` and records receipt (`sandbox.hostHelloSeen()`). Receipt also **closes the legacy paths**: a host that speaks hello has no business sending un-channelled frames or the `app_event` wrapper, so both are dropped and counted from then on. A host that never hellos keeps them.

Inbound `{ "channel":"system", "type":"goodbye" }` is logged.

---

## Framework modules

Privileged runtime code the sandbox hosts OUTSIDE the app — for board families that ship a Lua framework their apps program against. Resident is generic here: it hosts; it never interprets.

```cpp
Resident::SandboxConfig::FrameworkConfig fw;
fw.name = "myfw";          // announced in the hello — data, not semantics
fw.version = 3;
fw.source = MYFW_LUA;      // the built-in copy (cold-start default)
cfg.framework = fw;
```

**The environment boundary.** The framework chunk runs in a private environment whose `__index` is `_G`: it reads every baseline global, but its own assignments stay local — app code cannot reach them. Writing through `_G` is the explicit app-facing act: `_G.api = {...}` installs API for apps. The environment also holds the one capability apps never get: `runtime.send(name, data, opts?)` — the control-plane mirror of `events.send` (same queue, same three-state return), publishing on `channel:"runtime"`. Hosts can therefore trust runtime-channel traffic: no app-level code can forge it.

**Lifecycle hooks** (all optional; looked up in the framework env after its chunk runs):

| Hook | When | Contract |
|------|------|----------|
| `framework_install()` | Before each app chunk runs (after the fresh-environment reset) | (Re)install the framework's app-facing API into `_G` |
| `framework_tick(ctx, dt_ms)` | Every tick, before the app's `on_tick` | Same ctx shape as app callbacks |
| `framework_event(ctx, e)` | Every event, before the app's `on_event` | Return `true` to consume — the app never sees it |
| `framework_app_loaded()` | After an app loads successfully | — |

Under a framework, an app **need not** define any lifecycle global (`init`/`on_tick`/`on_event`) — an app defining none is normally rejected, and an active framework relaxes that, because validity is the framework's business. Apps may still define them: the framework's hooks run first, then the app's callbacks. Events are accepted and dispatched to the framework even when the app has no `on_event`.

**The framework slot.** `{channel:"system", type:"framework", name, version, code}` installs a replacement, persisted via the `PersistentStore` framework-slot virtuals (NVS key `resident/framework`) and loaded at boot in preference to the built-in. Empty `code` clears the slot and reverts to the built-in. A slot blob that fails to load is discarded (`framework_error`) and the built-in runs. Because only the system channel can carry this message, no sandboxed code — app or framework — can replace the framework. Each hello announces `framework: {name, version, source: "builtin"|"slot"}`, which is how a host decides to push an update.

Errors in framework hooks are contained like app errors (`runtime_error`, dispatch dies, everything survives), and every hook runs under the execution guard.

## The execution guard

Every protected Lua call — `init`, one tick, one event, one chunk, each framework hook — runs inside an arm/disarm pair. A runaway aborts *that dispatch* with a `runtime_error`; the app and the device carry on. Nested protected calls share the outermost dispatch's guard.

Two guards are available and `SandboxConfig` picks one.

**`executionBudget` — an instruction cap.** Deterministic, platform-independent, and the default (2,000,000 instructions). It is not a time bound: the same 2 M instructions land anywhere from ~740 ms to ~1.05 s depending on the instruction mix. It is also not free. Lua 5.4 raises a per-frame `trap` flag while any count hook is armed, so every VM instruction routes through `luaG_traceexec` for the whole dispatch, whether or not the hook body ever fires. Measured on an ESP32-S3 at 240 MHz, arming a 2 M count hook costs +87% on float arithmetic, +93% on table writes, and +171% on function calls, with zero hook fires.

**`executionDeadlineMs` — a wall-clock deadline.** Bounds time rather than instructions, and inverts the arming. No hook is installed at dispatch start; a one-shot `esp_timer` installs a stopping hook only once the deadline has passed. A dispatch that keeps to its deadline is completely unhooked and pays nothing — the same Lua-heavy shader that runs at 830 ms/tick under the instruction cap runs at 462 ms/tick under a 900 ms deadline, indistinguishable from running with no guard at all (463 ms). Only a runaway is taxed. Setting a hook from outside the running VM is Lua's own idiom for this (`lua.c`'s SIGINT handler); `OP_FORLOOP` and `dojump` refresh the cached `trap`, so even a bare `while true do a = a + 1 end` notices — measured aborting 1–9 ms past a 900 ms deadline. Device builds only.

**`executionSoftDeadlineMs` — a report, not a limit.** Compared against the elapsed time at disarm and reported (a rate-limited serial line plus a `slow_dispatch` telemetry event), never enforced. Because it measures after the fact it is the only part of the guard that sees time spent inside a blocking C binding — `screen.flip()`, an I2C read — where no Lua instruction executes and therefore no hook can fire. Independent of the other two, and available on host builds.

Choosing a hard deadline: it has to sit well above the heaviest dispatch the board legitimately runs, not at the 100 ms tick interval, or it kills honest apps. Measure the worst legitimate app and leave roughly 2x. The soft deadline is where "this app cannot hold 10 FPS" belongs.

**What is not guarded.** A dispatch blocked inside a C binding cannot be interrupted by either hook — no Lua instruction executes, so nothing can fire. The soft deadline notices the overshoot once the binding returns; nothing shortens it. The deadline also measures wall clock, not CPU: a dispatch preempted by a higher-priority task overshoots by however long it was descheduled, and aborts when the Lua task next runs.

## Writing a Driver

A Driver exposes hardware to Lua and optionally fires events back into `on_event`. Inherit from `Resident::Driver`, implement `name()` and `registerModule()`, and use `sendEvent()` to push events.

```cpp
#include <ResidentDriver.h>
#include <ResidentLuaModule.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

class ButtonDriver : public Resident::Driver {
public:
    explicit ButtonDriver(int pin) : _pin(pin) {}

    const char* name() const override { return "button"; }

    void begin() override {
        pinMode(_pin, INPUT_PULLUP);
    }

    void update() override {
        bool pressed = (digitalRead(_pin) == LOW);
        if (pressed && !_wasPressed) {
            Resident::EventField fields[] = {
                { "pin", Resident::EventField::INT, { .i = _pin } },
            };
            sendEvent("button", fields, 1);
        }
        _wasPressed = pressed;
    }

    void registerModule(Resident::LuaModule& m) override {
        m.method<ButtonDriver, &ButtonDriver::luaIsPressed>("is_pressed");
    }

    int luaIsPressed(lua_State* L) {
        lua_pushboolean(L, _wasPressed);
        return 1;
    }

private:
    int  _pin;
    bool _wasPressed = false;
};
```

Register the driver with the sandbox:

```cpp
ButtonDriver btn{9};

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.extensions = {&btn};
    return cfg;
}
```

Then in Lua:

```lua
function on_event(ctx, event)
    if event.name == "button" then
        log.info("button pin=" .. tostring(event.data.pin))
    end
end
```

If the driver also implements `SystemDisplay`, declare `Driver` first — see [Inheritance ordering rule](#inheritance-ordering-rule).

---

## Writing an Extension

Use `Extension` (not `Driver`) when you only need a Lua module with no hardware events.

```cpp
#include <ResidentExtension.h>
#include <ResidentLuaModule.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

class StorageExtension : public Resident::Extension {
public:
    const char* name() const override { return "storage"; }

    void registerModule(Resident::LuaModule& m) override {
        m.method<StorageExtension, &StorageExtension::luaGet>("get")
         .method<StorageExtension, &StorageExtension::luaSet>("set");
    }

    int luaGet(lua_State* L) {
        const char* key = luaL_checkstring(L, 1);
        // read from persistent store, push result
        lua_pushstring(L, readValue(key));
        return 1;
    }

    int luaSet(lua_State* L) {
        const char* key = luaL_checkstring(L, 1);
        const char* val = luaL_checkstring(L, 2);
        writeValue(key, val);
        return 0;
    }

    void onAppReset() override {
        // called before each new app loads — clear any per-app state
    }

private:
    const char* readValue(const char* key);
    void writeValue(const char* key, const char* val);
};
```

In Lua:

```lua
function init(ctx)
    local saved = storage.get("color")
    if saved then
        log.info("restored color: " .. saved)
    end
end
```

---

## Vendoring consumers (ESP-IDF)

Resident's ESP-IDF `CMakeLists.txt` REQUIRES list defaults to ESP Component
Registry-namespaced names (`inanimate__courier`, `bblanchon__arduinojson`).
If your project vendors courier / ArduinoJson / Esp32Lua under bare
directory names rather than fetching them via the registry, override the
names from your project's root `CMakeLists.txt` BEFORE the
`include($ENV{IDF_PATH}/tools/cmake/project.cmake)` line:

~~~cmake
set(RESIDENT_COURIER_DEP     "courier"     CACHE STRING "" FORCE)
set(RESIDENT_ARDUINOJSON_DEP "ArduinoJson" CACHE STRING "" FORCE)
~~~

Or pass on the `idf.py` command line: `idf.py -DRESIDENT_COURIER_DEP=courier ...`.

The four cache vars are:

| Cache var | Default | Override to (vendored) |
|-----------|---------|------------------------|
| `RESIDENT_COURIER_DEP` | `inanimate__courier` | `courier` |
| `RESIDENT_ARDUINOJSON_DEP` | `bblanchon__arduinojson` | `ArduinoJson` |
| `RESIDENT_EZTIME_DEP` | `""` (empty) | `ezTime` |
| `RESIDENT_ESP32LUA_DEP` | `Esp32Lua` | `Esp32Lua` (already bare) |

`RESIDENT_EZTIME_DEP` defaults empty because `inanimate__courier` already
bundles ezTime via CMake FetchContent on the registry path — no separate
component is needed. Vendoring consumers who manage ezTime as a standalone
component (e.g. via a git submodule) should set `RESIDENT_EZTIME_DEP=ezTime`
so that ESP-IDF's strict header-required-component check passes.

`arduino-esp32` is hard-coded as `espressif__arduino-esp32` — vendoring
consumers don't typically vendor it; it comes from the registry on both paths.

PlatformIO consumers are unaffected — these cache vars only apply to the
ESP-IDF CMake component graph.

---

## Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `Extensions::MAX` | `12` | Maximum extensions per sandbox |
| `RenderTargets::MAX` | `8` | Maximum registered render targets per board |
| `Sandbox::TICK_INTERVAL` | `100 ms` | Lua `on_tick` interval (10 FPS) |
| `RESIDENT_EVENT_RING_SIZE` | `8` | Inbound event ring depth (one slot kept free, so 7 usable); oldest event is dropped when full. Build-flag overridable |
| `RESIDENT_EVENT_QUEUE_SIZE` | `16` | Outbound event queue depth (rate-limited / offline sends wait here). Build-flag overridable |
| `RESIDENT_EVENT_JSON_MAX` | `1024` | Event `data` buffer — bounds the `events.send` serializer, the incoming app-channel `data`, and each ring slot. Build-flag overridable; the ring grows by 8× any increase |
| `RESIDENT_STORE_JSON_MAX` | `2048` | Serialized size cap on the whole persisted `store` blob. Build-flag overridable |
| `StoreModule::STORE_NS_MAX` | `32` | Maximum `storeNs` length in characters |
| Event `name` max | `32 chars` | `Event::name` buffer size — driver event names longer than 31 bytes are truncated |
| `SandboxConfig::executionBudget` | `2000000` | Lua instructions per dispatch (`0` = unlimited) |
| `SandboxConfig::executionDeadlineMs` | `0` | Wall-clock milliseconds per dispatch before abort (`0` = use the instruction cap) |
| `SandboxConfig::executionSoftDeadlineMs` | `0` | Wall-clock milliseconds per dispatch before reporting (`0` = no reporting) |
| `SLOW_DISPATCH_COOLDOWN` | `5000 ms` | Minimum interval between `slow_dispatch` reports after the initial burst |
| `SLOW_DISPATCH_MAX_BURST` | `3` | `slow_dispatch` reports allowed before the cooldown applies |
| Event rate limit | `5/s`, burst `10` | Token bucket shared by `events.send`, `runtime.send` and `publishEvent` |
| `RUNTIME_ERROR_COOLDOWN` | `5000 ms` | Minimum interval between `runtime_error` telemetry emissions from `on_tick` |
| `RUNTIME_ERROR_MAX_BURST` | `3` | Number of `runtime_error` telemetry events allowed before rate-limiting kicks in |
