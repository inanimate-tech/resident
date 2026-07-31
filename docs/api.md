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
| `extensions` | `Extensions` | `{}` | Drivers and extensions registered with the sandbox (registration order is preserved across `begin()` / `registerModule()` / `update()` / `onAppReset()`) |
| `shaderTemplate` | `ShaderTemplateFn` | `nullptr` | Function that converts shader fields into Lua source (see [Message Protocol](#message-protocol)) |
| `telemetry` | `TelemetryCallback` | `nullptr` | Called with outgoing telemetry JSON strings (also settable later via `sandbox.setTelemetryCallback`) |
| `timezone` | `const char*` | `nullptr` | IANA timezone string applied at construction (e.g. `"Europe/London"`); also settable later via `sandbox.setTimezone` |
| `systemDisplay` | `SystemDisplay*` | `nullptr` | Optional text display; Resident's internal handler calls `displayText()` automatically on connection state changes |
| `systemLED` | `SystemLED*` | `nullptr` | Optional LED indicator; Resident's internal handler calls `solidColor()` automatically on connection state changes |
| `network` | `std::optional<Courier::Config>` | unset | Networking opt-in. Set ⇒ Sandbox constructs an internal `Courier::Client`, drives WiFi / transports, fires connection callbacks. Unset ⇒ standalone runtime, no WiFi pulled in. |
| `persistApps` | `bool` | `true` | Save the last successfully-loaded app to flash and restore it on boot. Set to `false` to disable for a build. |
| `systemButton` | `Resident::SystemButton*` | `nullptr` | Optional button the runtime polls to skip the boot countdown (and, via `onSystemButtonHold`, a runtime hold gesture). Implement `Resident::SystemButton` and pass a pointer here. |
| `systemMic` | `Resident::SystemMic*` | `nullptr` | Optional microphone the runtime streams via the mic pump (see [SystemMic](#residentsystemmic)). On M5 boards use the shipped `Resident::M5Mic` (`#include <ResidentM5Mic.h>`); otherwise implement `Resident::SystemMic`. Not a `Driver` — the pump owns its `begin()`/`end()`. |
| `persistentStore` | `Resident::PersistentStore*` | `nullptr` | Override the backing store for persistence. `nullptr` uses NVS on device; inject a fake in tests. |
| `tickSliceMs` | `uint32_t` | `8` | Wall-clock budget for one continuous slice of `on_tick`. A frame that outruns it yields and resumes on the next `loop()` pass (see [Tick slicing](#tick-slicing)). `0` disables slicing. |
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
   4. The Lua `on_tick(ctx, dt_ms)` callback fires at 10 FPS (100 ms interval) — only while an app is Running, and (when networked) only once connected. Standalone always ticks unconditionally.
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

Three tools for platform wrappers that need to sit in front of the sandbox's message routing without re-implementing it. Their reach differs since channel routing landed: `onMessageFilter` is **legacy un-channelled path only**; `deferAppLoads` and `injectMessage` apply across both the legacy path and channel routing (see [Channel routing](#channel-routing)).

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
| `"app"` | `handleAppMessage` → Lua `on_event(ctx, event)` with `event.name = type` | Data plane. No reserved types here — `type:"forget"` on this channel is just an event, not a persistence op. Self-echo (`from == getDeviceId()`) and duplicate `nonce` (16-entry ring, exact match) are dropped before delivery. Gated like `sendAppEvent`: dropped when no app is loaded or the app defines no `on_event`. The legacy `app_event` envelope (`{"type":"app_event","name":...,"data":...}`) is still accepted here for one release, logging `[deprecated] app_event wrapper; send channel:"app" with type=<event name>`. |
| `"system"` | `handleSystemMessage` | Control plane. Reserved types `app`/`shader`/`forget` are handled exactly as on the legacy path (including `deferAppLoads` and the `description` display below); any other type falls through to the single `"system"` slot registered via `onMessageWithChannel("system", cb)`. |
| anything else | the matching slot registered via `onMessageWithChannel(name, cb)` | Single slot per channel name (exact string match), last registration wins. An unregistered channel is logged (`Resident::Sandbox: no handler for channel '<channel>' (type '<type>'); dropped`) and the message is dropped. Up to 8 slots (`onMessageWithChannel` does not include `"app"` — the data plane belongs to the Lua app, not a C++ slot). |
| *(absent)* | the legacy un-channelled path | `[deprecated] un-channelled '<type>' message; sender should stamp channel`, then `onMessageFilter` → deferral → reserved-type routing → `onMessage` — unchanged from before channel routing. |

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

**Data-plane emit** — `sandbox.publishEvent(name, dataJson)` builds `{channel:"app", type:name, data, from:getDeviceId(), nonce, ts_ms}` and hands it to the event sink: the sink set via `setEventSink(EventSink)` if one is registered, otherwise `courier().send` on the default transport. Rate-limited with a token bucket (5 events/s sustained, burst of 10); returns `false` on rate limit, no sink/network, or send failure — it never raises. This is the shared implementation behind the Lua `events.send` (see [Lua API](#events-module)) and any C++ caller, e.g. a platform wrapper's `room.announce` alias — note this is a **deliberate behavior change** from the old `room.announce`, which raised a Lua error on rate limit rather than returning `false`.

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
sandbox.sendAppEvent(name, dataJson);  // queue an app_event to the running app
sandbox.onMessageWithChannel(name, cb); // register a channel slot (see Channel routing)
sandbox.sendSystem(doc);               // stamp channel:"system", send via default transport
sandbox.publishEvent(name, dataJson);  // stamp channel:"app" event, rate-limited, send via sink
sandbox.setEventSink(fn);              // override publishEvent's/events.send's destination
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
sandbox.startMicStream();              // stream systemMic frames (default sink: ws().sendBinary)
sandbox.stopMicStream();               // stop streaming
sandbox.isMicStreaming();              // true while streaming
sandbox.setMicStreamSink(fn);          // override the binary frame sink
sandbox.generationId();                // const String& — ID of the last loaded app/shader
sandbox.setTelemetryCallback(cb);      // wire telemetry JSON to your transport
sandbox.clearPersistedApp();           // wipe the saved app from the persistent store
```

`loadApp` stops any running app, calls `onAppReset()` on all extensions, generates a new `generationId`, and compiles the new app. An app must define at least one of `init`, `on_tick`, or `on_event` — compilation is rejected otherwise.

`loadShader` requires `SandboxConfig::shaderTemplate` to be set; it converts the `ShaderFields` map to Lua source, then calls `loadApp`.

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
- `loop()` ticks Lua at 10 FPS unconditionally (no gating on connection state).
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

Queues a driver event into the sandbox event ring. The event appears in Lua as `on_event(ctx, event)` with the event fields flattened directly onto the `event` table.

```cpp
// In a button driver's ISR or debounce handler:
EventField fields[] = {
    { "id",    EventField::INT,    { .i = buttonId } },
    { "state", EventField::STRING, { .s = "pressed" } },
};
sendEvent("button", fields, 2);
```

The event name `"button"` is special: it increments `ctx.trigger_count` for every app tick until the next app load.

### EventField struct

```cpp
struct EventField {
    const char* key;
    enum Type { INT, STRING } type;
    union {
        int         i;
        const char* s;
    };
};
```

| Field | Description |
|-------|-------------|
| `key` | Field name — appears as `event.<key>` in Lua. Max 32 chars (event name buffer). |
| `type` | `EventField::INT` or `EventField::STRING` |
| `i` | Integer value (when `type == INT`) |
| `s` | String value (when `type == STRING`) |

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
- **Connectivity gates neither** `update()`. (The Lua `on_tick` still waits
  for the first connection on networked boards.)

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
| `Extensions::MAX` | `8` | Maximum number of extensions per sandbox |

Extensions are stored in registration order. `begin()`, `registerModule()`, `update()`, and `onAppReset()` are all called in registration order.

The user owns the extension instances (typically global or static variables). The `Extensions` struct holds raw pointers and does not manage lifetime.

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

`startMicStream()` returns `false` (and does not start streaming) when no `systemMic` is configured or its `begin()` fails. While streaming, each `Sandbox::loop()` reads up to `frameSamples()` (capped at an internal 512-sample buffer) and forwards the bytes to the sink — the default sink is `ws().sendBinary`. The pump adds **no framing or control frames**: any envelope (e.g. a `{"type":"..."}` start/stop text frame, or a format handshake) is a device concern, sent by your code around `startMicStream()` / `stopMicStream()`. Streaming is independent of app state — it continues while the app is suspended.

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
| `trigger_count` | integer | Number of `"button"` driver events since the last app load |
| `utc_h` | integer | Current UTC hour (0–23) |
| `utc_m` | integer | Current UTC minute (0–59) |
| `localtime_h` | integer | Local hour — equals `utc_h` unless a timezone has been set |
| `localtime_m` | integer | Local minute — equals `utc_m` unless a timezone has been set |

`localtime_h` / `localtime_m` reflect local time only after `Sandbox::setTimezone` succeeds. Otherwise they are equal to `utc_h` / `utc_m`.

Every field above is sampled **once, when the frame starts**, and stays fixed
for the frame's whole life even if it spans several slices (see
[Tick slicing](#tick-slicing)). Motion computed against `time_ms` or `dt_ms`
is therefore stable regardless of how expensive the frame is.

### Tick slicing

`on_tick` runs on a coroutine with a wall-clock budget of
`SandboxConfig::tickSliceMs` (default 8 ms). A frame that finishes inside its
budget behaves exactly like a plain call. A frame that outruns it is yielded
mid-flight: `Sandbox::loop()` returns, the rest of the system runs, and the
**same frame** resumes on the next pass.

```
loop() pass 1:  [courier][drivers][overlays]  frame slice ─┐
loop() pass 2:  [courier][drivers][overlays]  frame slice ─┤ one on_tick
loop() pass 3:  [courier][drivers][overlays]  frame slice ─┘
loop() pass 4:  [courier][drivers][overlays]  FRAME READY → next frame
```

An expensive app therefore costs frame rate, not system responsiveness —
networking, driver polling and overlay drawing keep running at full loop rate
throughout. The app is told the truth about the cost: `dt_ms` spans
frame-start to frame-start, so a frame that takes a second reports ~1000 ms
next time round, and time-based motion stays correct at the lower rate.

Guarantees while a frame is in flight:

- No second frame starts — frames never overlap.
- `on_event` does not run. A queued event dispatches once the frame completes,
  so a handler never observes half-updated app state.
- `loadApp`, `loadShader` and `suspendApp` abandon the frame cleanly.

Set `tickSliceMs = 0` to opt out and run each frame as one uninterrupted call.

**Limitation:** a frame running below a C-call boundary — an extension binding
that called back into Lua — has no resumable continuation and cannot be
yielded. Such a frame runs to completion in the current pass.

### `event` table

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Event name (e.g. `"button"`, `"my_event"`) |
| `from` | string | Source identifier — empty string for driver events |
| `ts_ms` | integer | Timestamp in milliseconds (`millis()`) when the event was queued |
| *(driver fields)* | any | For **driver events**: extra fields are flattened directly onto the table (e.g. `event.id`, `event.state`) |
| `data` | table | For **app_events**: the JSON `data` object parsed into a subtable |

```lua
function on_event(ctx, event)
    if event.name == "button" then
        -- driver event: fields flattened directly
        log.info("button " .. tostring(event.id))
    elseif event.name == "update" then
        -- app_event: data is a subtable
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
| `events.send(name)` | boolean | Publish `name` with no data |
| `events.send(name, data)` | boolean | Publish `name` with a flat table of string/number values as `event.data` |

```lua
events.send("turn")
events.send("color_change", { hue = 180, label = "warm" })
```

`data` must be a **flat** table — string and number values only (booleans, nested tables, and other types are silently skipped per-key). Serialized to a bounded 256-byte JSON buffer; oversized payloads are truncated.

Rate-limited by a shared token bucket: 5 events/s sustained, burst of 10. Returns `false` (rather than raising a Lua error) when rate-limited, when the event name is empty, or when the underlying send fails — always check the return value if you need to know whether it went out.

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

This section describes the reserved types on the legacy un-channelled path (no `channel` field) — see [Channel routing](#channel-routing) for the full envelope picture, including the `"app"` data plane and custom channels. Resident routes four JSON message types (`app`, `shader`, `app_event`, `forget`) internally on this path — they never reach the user's `onMessage(cb)` callback. Any other type is forwarded to `onMessage` if registered. The `"system"` channel handles `app`/`shader`/`forget` identically (same `loadApp`/`loadShader`/`clearPersistedApp` calls, same deferral and description-display behavior) but has no `app_event` — the app data plane is `channel:"app"`, documented under Channel routing.

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

The sandbox emits telemetry events via `TelemetryCallback`. Format:

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

Wire the callback up before `setup()` to forward telemetry over the connected WebSocket transport:

```cpp
sandbox.setTelemetryCallback([](const char* json) {
    sandbox.ws().sendText(json);
});
```

---

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
        log.info("button pin=" .. tostring(event.pin))
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
| `Extensions::MAX` | `8` | Maximum extensions per sandbox |
| `Sandbox::TICK_INTERVAL` | `100 ms` | Lua `on_tick` interval (10 FPS) |
| `Sandbox::SANDBOX_MAX_EVENTS` | `8` | Event ring buffer capacity; oldest event is dropped when full |
| Event `name` max | `32 chars` | `Event::name` buffer size — driver event names longer than 31 bytes are truncated |
| Event `data` max | `256 chars` | `Event::data` buffer — serialized driver event fields or `app_event` JSON |
| `RUNTIME_ERROR_COOLDOWN` | `5000 ms` | Minimum interval between `runtime_error` telemetry emissions from `on_tick` |
| `RUNTIME_ERROR_MAX_BURST` | `3` | Number of `runtime_error` telemetry events allowed before rate-limiting kicks in |
