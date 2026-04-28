# Resident API Reference

## Configuration

All configuration uses a struct-and-assign pattern compatible with C++11. `DeviceConfig` is in `ResidentDeviceConfig.h` (pulled in by `ResidentDevice.h`); `SandboxConfig` is in `ResidentSandboxConfig.h` (pulled in by `ResidentSandbox.h`).

For global instances, use a factory function so construction happens after static init:

```cpp
#include <ResidentDevice.h>

Resident::DeviceConfig makeConfig() {
    Resident::DeviceConfig cfg;
    cfg.deviceType = "my-device";
    cfg.host       = "api.example.com";
    cfg.extensions = {&myDisplay, &myButton};
    return cfg;
}

class MyDevice : public Resident::Device {
public:
    MyDevice() : Resident::Device(makeConfig()) {}
};

MyDevice device;

void setup() { device.setup(); }
void loop()  { device.loop(); }
```

### DeviceConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `deviceType` | `const char*` | `nullptr` | Device type string — sent to the server during WebSocket handshake |
| `host` | `const char*` | `nullptr` | Server hostname |
| `statusLED` | `StatusLED*` | `nullptr` | Optional LED indicator; `Device` drives it on connection state changes |
| `statusDisplay` | `StatusDisplay*` | `nullptr` | Optional text display; `Device` drives it with status messages |
| `shaderTemplate` | `ShaderTemplateFn` | `nullptr` | Function that converts shader fields into Lua source (see [Message Protocol](#message-protocol)) |
| `extensions` | `Extensions` | `{}` | Drivers and extensions registered with the sandbox |

The `extensions` field is filled with a brace-list of `Extension*` pointers in registration order:

```cpp
cfg.extensions = {&displayDriver, &buttonDriver, &imuDriver};
```

### SandboxConfig

Use `SandboxConfig` when running the sandbox standalone (without `Device`).

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `extensions` | `Extensions` | `{}` | Drivers and extensions registered with the sandbox |
| `shaderTemplate` | `ShaderTemplateFn` | `nullptr` | Shader-to-Lua template function |
| `telemetry` | `TelemetryCallback` | `nullptr` | Called with outgoing telemetry JSON strings |
| `timezone` | `const char*` | `nullptr` | IANA timezone string applied at construction (e.g. `"Europe/London"`) |

```cpp
#include <ResidentSandbox.h>

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.extensions = {&myLED};
    cfg.timezone   = "America/New_York";
    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};
```

---

## Resident::Device

`Resident::Device` composes Courier for connectivity with the Lua sandbox. Subclass it to add project-specific hardware and message handling.

Include with:

```cpp
#include <ResidentDevice.h>
```

### Lifecycle

```cpp
device.setup();   // initialize WiFi, Courier, and sandbox — call from Arduino setup()
device.loop();    // drive networking and sandbox — call from Arduino loop()
```

### Construction and subclassing

```cpp
class MyDevice : public Resident::Device {
public:
    MyDevice() : Resident::Device(makeConfig()) {}

protected:
    void deviceSetup() override {
        // called at end of setup() — hardware init, telemetry wiring
        sandbox().setTelemetryCallback([](const char* json) {
            courier().send(json);
        });
    }

    void deviceLoop() override {
        // called every loop() — after Courier and sandbox have ticked
    }
};
```

### Accessors

```cpp
device.courier();          // Courier& — the underlying Courier instance
device.sandbox();          // Sandbox& — the Lua sandbox
device.isConnected();      // true when Courier is fully connected
device.getDeviceId();      // String — device ID assigned by the server
device.isTimeSynced();     // true after NTP/HTTP time sync
device.getDeviceType();    // const char* — from DeviceConfig::deviceType
```

### Sending

```cpp
device.send(payload);                           // default transport
device.sendTo("ws", payload);                   // named transport
device.sendBinaryTo("ws", data, len);           // binary, named transport
```

### Message handling

Override `onMessage` to handle custom message types. Call the super implementation to keep sandbox routing for `app`, `shader`, and `app_event` messages:

```cpp
void onMessage(const char* type, JsonDocument& doc) override {
    if (strcmp(type, "config") == 0) {
        // handle custom type
        return;
    }
    Resident::Device::onMessage(type, doc);  // route app/shader/app_event to sandbox
}
```

### Connection lifecycle hooks

All three hooks call up the chain by default. Override and call super to extend:

```cpp
void onConnectionChange(CourierState state) override {
    // fires on every state transition
    Resident::Device::onConnectionChange(state);
}

void onTransportsWillConnect() override {
    // fires before transports start — good place for registration/token exchange
    Resident::Device::onTransportsWillConnect();
}

void onConnected() override {
    // fires when fully connected
    Resident::Device::onConnected();
}
```

### Subclass hooks

| Method | Description |
|--------|-------------|
| `deviceSetup()` | Called at the end of `setup()`. Hardware init, telemetry wiring. |
| `deviceLoop()` | Called every `loop()` after Courier and sandbox have ticked. |
| `buildWebSocketPath()` | Returns `String` — override to customize the WebSocket path. Default is `/agents/<deviceType>-agent/<deviceId>`. |

### Status accessors (protected)

```cpp
statusLED();       // StatusLED* from config — nullptr if not set
statusDisplay();   // StatusDisplay* from config — nullptr if not set
```

---

## Resident::Sandbox

The Lua sandbox. Used standalone (no network) or accessed via `Device::sandbox()`.

Include with:

```cpp
#include <ResidentSandbox.h>
```

### Construction and configuration

```cpp
Resident::Sandbox sandbox;                      // default-constructed, configure later
Resident::Sandbox sandbox{makeConfig()};        // constructed with config
sandbox.configure(cfg);                         // replace config (not additive)
```

`Device::setup()` calls `configure()` automatically — downstream Device subclasses normally do not call it directly.

### Lifecycle

```cpp
sandbox.initialize();   // create Lua state, register extensions, set up globals
sandbox.loop();         // drive extension update() and Lua on_tick at 10 FPS
```

Call `initialize()` once before `loop()`. On ESP32, the Lua allocator routes all allocations to PSRAM.

### App loading

```cpp
sandbox.loadApp(luaCode);            // compile and run a Lua source string
sandbox.loadShader(fields);          // generate Lua via ShaderTemplateFn, then loadApp
sandbox.sendAppEvent(name, dataJson); // queue an app_event to the running app
sandbox.isAppRunning();              // true when an app is compiled and active
sandbox.generationId();              // const String& — ID of the last loaded app/shader
```

`loadApp` stops any running app, calls `onAppReset()` on all extensions, generates a new `generationId`, and compiles the new app. An app must define at least one of `init`, `on_tick`, or `on_event` — compilation is rejected otherwise.

`loadShader` requires `SandboxConfig::shaderTemplate` to be set; it converts the `ShaderFields` map to Lua source, then calls `loadApp`.

### Timezone

```cpp
sandbox.setTimezone("Europe/London");   // set IANA timezone; performs UDP lookup on first use
sandbox.hasTimezone();                  // true after a successful setTimezone call
```

ezTime looks up the POSIX rule via `timezoned.rop.nl` on first sight of a zone and caches in EEPROM. On failure (null, empty, or unrecognised zone) the sandbox falls back to UTC and `hasTimezone()` returns `false`. Timezone affects `ctx.localtime_h`, `ctx.localtime_m`, `time.hour()`, `time.minute()`, and `time.second()` in Lua.

### Telemetry

```cpp
sandbox.setTelemetryCallback([](const char* json) {
    courier.send(json);
});
```

The callback receives a complete JSON string for each telemetry event. Format: `{ "type": "telemetry", "generationId": "...", "name": "...", "data": {...} }`. See [Message Protocol](#message-protocol).

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
| `begin()` | no-op | Hardware / module init — called once by `Sandbox::initialize()` |
| `update()` | no-op | Per-loop tick at full main-loop rate (not Lua's 10 FPS) |
| `onAppReset()` | no-op | Called before each new app is compiled |

### Idempotent early init

```cpp
Resident::Extension::beginExtension(myExtension);
```

Call this before `sandbox.initialize()` to run `begin()` early (e.g. a status display that must be ready before the sandbox). `Sandbox::initialize()` calls `beginExtension` on every extension; the second call is a no-op.

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

A fixed-capacity list of `Extension*` pointers passed to `DeviceConfig` or `SandboxConfig`.

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

Interface for connection-state text output. Implement it in a display driver and pass a pointer via `DeviceConfig::statusDisplay`.

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
| `displayText(const char* text)` | *(pure virtual)* | Show a status string — called by `Device` on connection state changes |
| `begin()` | no-op | `Device` calls once at `setup()` |
| `update()` | no-op | `Device` calls every `loop()` |

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
| `solidColor(uint32_t color)` | *(pure virtual)* | Set the LED to a packed `0xRRGGBB` color — called by `Device` on connection state changes |

`Device` calls `solidColor` automatically as the connection state changes (e.g. red while connecting, green when connected). There are no `begin()` or `update()` lifecycle hooks — initialize LED hardware in your subclass constructor or in `deviceSetup()`.

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

When using `Resident::Device`, the base `onMessage()` routes these JSON message types to the sandbox. Call `Device::onMessage(type, doc)` from your override to keep this routing.

### `app` — load a Lua app

```json
{ "type": "app", "code": "function on_tick(ctx, dt_ms) ... end" }
```

Calls `Sandbox::loadApp(doc["code"])`. Any previously running app is stopped first.

### `shader` — load a shader expression

```json
{ "type": "shader", "expr": "rgb(fract(ctx.time_ms / 2000.0), 0, 0)" }
```

The entire JSON document (as a `ShaderFields` map of string key/value pairs) is passed to `DeviceConfig::shaderTemplate`, which must return valid Lua source. The result is passed to `loadApp`. Requires `shaderTemplate` to be set.

### `app_event` — send an event to the running app

```json
{ "type": "app_event", "name": "color_change", "data": { "hue": 180 } }
```

Calls `Sandbox::sendAppEvent(name, dataJson)`. The event arrives in Lua as `on_event(ctx, event)` with `event.data` set to the parsed `data` object.

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

Wire the callback in `deviceSetup()`:

```cpp
void deviceSetup() override {
    sandbox().setTelemetryCallback([this](const char* json) {
        send(json);
    });
}
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

Resident::DeviceConfig makeConfig() {
    Resident::DeviceConfig cfg;
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
