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
| `statusDisplay` | `StatusDisplay*` | `nullptr` | Optional text display; Resident's internal handler calls `displayText()` automatically on connection state changes |
| `statusLED` | `StatusLED*` | `nullptr` | Optional LED indicator; Resident's internal handler calls `solidColor()` automatically on connection state changes |
| `network` | `std::optional<Courier::Config>` | unset | Networking opt-in. Set ⇒ Sandbox constructs an internal `Courier::Client`, drives WiFi / transports, fires connection callbacks. Unset ⇒ standalone runtime, no WiFi pulled in. |
| `persistApps` | `bool` | `true` | Save the last successfully-loaded app to flash and restore it on boot. Set to `false` to disable for a build. |
| `systemButton` | `Resident::SystemButton*` | `nullptr` | Optional button the runtime polls to skip the boot countdown. Implement `Resident::SystemButton` and pass a pointer here. |
| `persistentStore` | `Resident::PersistentStore*` | `nullptr` | Override the backing store for persistence. `nullptr` uses NVS on device; inject a fake in tests. |

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
   4. The `StatusDisplay::begin()` lifecycle runs (if a `statusDisplay` is configured).
   5. The sandbox itself initialises: Lua state is created, extensions get `begin()` and `registerModule()` calls in registration order, globals are registered.
   6. `Courier::Client::setup()` runs, which kicks WiFi and transports. During this:
      - `onCourierConnectionChange` fires for each state transition (`WifiConnecting` → `WifiConnected` → `TransportsConnecting` → `Connected`, etc.). Internal handler updates `statusDisplay`/`statusLED`, then the user's `onConnectionChange(cb)` callback fires.
      - `onCourierTransportsWillConnect` fires once before transports begin. Internal handler sets the default `/agents/<type>-agent/<id>` WS path, then the user's `onTransportsWillConnect(cb)` callback fires (override the path here).
      - `onCourierConnected` fires when fully connected. The user's `onConnected(cb)` callback runs.
3. **`loop()`** — in order:
   1. `Courier::Client::loop()` drives the network state machine and reads transports.
   2. `StatusDisplay::update()` (if configured).
   3. If networked, gates the Lua tick on `isConnected()`. Standalone always ticks.
   4. Every extension's `update()` runs at full main-loop rate.
   5. The Lua `on_tick(ctx, dt_ms)` callback fires at 10 FPS (100 ms interval).
   6. Up to one pending event is delivered to `on_event(ctx, event)`.

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

sandbox.onMessage([](const char* transport, const char* type, JsonDocument& doc) {
    if (strcmp(type, "config") == 0) {
        // handle a custom message type
    }
});

sandbox.onConnectionChange([](Courier::State state) {
    // fires on every state transition; statusDisplay/statusLED are already updated
});

sandbox.onConnected([]() {
    // fires when fully connected — good place to load a bootstrap app
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    sandbox.loadApp(bootstrapLua);
});
```

| Callback | Signature | Fires |
|----------|-----------|-------|
| `onTransportsWillConnect` | `void()` | Once, after Resident sets the default WS path and before transports start. Override the path here. |
| `onMessage` | `void(const char* transport, const char* type, JsonDocument&)` | For **non-reserved** message types only. Reserved types (`app`, `shader`, `app_event`) are routed internally — no super-call is needed. |
| `onConnectionChange` | `void(Courier::State)` | On every state transition. Resident's internal handler updates `statusDisplay`/`statusLED` first; your callback runs alongside (does not replace). |
| `onConnected` | `void()` | When fully connected. Often used to load a bootstrap app — guard with a function-local `static bool loaded` to avoid re-firing on reconnect. |

### Sandbox controls

```cpp
sandbox.loadApp(luaCode);              // compile and run a Lua source string
sandbox.loadShader(fields);            // generate Lua via ShaderTemplateFn, then loadApp
sandbox.sendAppEvent(name, dataJson);  // queue an app_event to the running app
sandbox.setTimezone("Europe/London");  // IANA zone — performs UDP lookup on first use
sandbox.hasTimezone();                 // true after a successful setTimezone call
sandbox.isAppRunning();                // true when an app is compiled and active
sandbox.suspendApp();                  // pause the running app's tick without unloading it
sandbox.resumeApp();                   // resume a suspended app
sandbox.isAppSuspended();              // true between suspendApp() and resumeApp()
sandbox.generationId();                // const String& — ID of the last loaded app/shader
sandbox.setTelemetryCallback(cb);      // wire telemetry JSON to your transport
sandbox.clearPersistedApp();           // wipe the saved app from the persistent store
```

`loadApp` stops any running app, calls `onAppReset()` on all extensions, generates a new `generationId`, and compiles the new app. An app must define at least one of `init`, `on_tick`, or `on_event` — compilation is rejected otherwise.

`loadShader` requires `SandboxConfig::shaderTemplate` to be set; it converts the `ShaderFields` map to Lua source, then calls `loadApp`.

`suspendApp` pauses the Lua tick (`on_tick` and event dispatch) without unloading the app — Courier and extension `update()` keep running. While suspended, drivers receive `onAppRunning(false)` so the status display is freed for direct text (e.g. a "Listening" overlay via `StatusDisplay::displayText()`); `resumeApp` reverses this with `onAppRunning(true)`. Both are no-ops when no app is loaded, and repeated calls don't re-notify. `isAppRunning()` stays `true` while suspended — suspension is a separate axis queried via `isAppSuspended()`. Events arriving while suspended are queued, not dropped (though a long suspend can overflow the 8-slot ring, losing the oldest), and `loadApp` always clears suspension.

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

When a Driver also implements another interface (e.g. `StatusDisplay`), `Driver` must be the **leftmost** base class:

```cpp
// OK — Driver (and therefore Extension) is leftmost
class MyDriver : public Resident::Driver, public Resident::StatusDisplay { ... };

// BROKEN — StatusDisplay is leftmost; the LuaModule trampoline cast will be wrong
class MyDriver : public Resident::StatusDisplay, public Resident::Driver { ... };
```

This matters because `LuaModule::method<>` casts the stored `Extension*` pointer directly to the `Class*` type. The cast is valid only when `Extension` is the leftmost base — i.e. when `static_cast<Class*>(extensionPtr)` produces the same address. See [Resident::LuaModule](#residentluamodule) for details.

### When to use Extension vs Driver

- Use `Extension` when you only register a Lua module (read sensors, control outputs from Lua, but no driver-generated events).
- Use `Driver` when your extension needs to push events into Lua (`sendEvent`) or respond to app start/stop (`onAppRunning`).

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

## Resident::StatusDisplay

Interface for connection-state text output. Implement it in a display driver and pass a pointer via `SandboxConfig::statusDisplay`.

```cpp
class MyDisplay : public Resident::StatusDisplay {
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

A Driver can implement `StatusDisplay` as a second interface for dual-use hardware (display + driver). Follow the [inheritance ordering rule](#inheritance-ordering-rule) — `Driver` must come first.

---

## Resident::StatusLED

Interface for a simple LED indicator driven by connection state.

```cpp
class MyLED : public Resident::StatusLED {
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

Resident's internal handler calls `solidColor` automatically as the connection state changes (yellow during WiFi setup, cyan while transports connect, green when connected, orange while reconnecting, red on failure). There are no `begin()` or `update()` lifecycle hooks — initialize LED hardware in your subclass constructor or before `sandbox.setup()`.

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

Resident routes three JSON message types internally — they never reach the user's `onMessage(cb)` callback. Any other type is forwarded to `onMessage` if registered.

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

On boot, if a saved app exists, the device shows its identity and a 20-second countdown on the status display before loading it:

```
Device type: <deviceType>
Device ID: <deviceId>

20s
```

You need the device ID to push apps to the device, so the countdown is a reminder. It is a timer (not press-to-continue) because not every board has a button. The countdown is skipped early by a configured `SystemButton` press or by an app/shader arriving over the network.

When **no** app is loaded — a fresh device, or after a load fails — the status display rests on the same identity screen without the countdown line:

```
Device type: <deviceType>
Device ID: <deviceId>
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

If the driver also implements `StatusDisplay`, declare `Driver` first — see [Inheritance ordering rule](#inheritance-ordering-rule).

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
