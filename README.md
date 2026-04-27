# Outrun

Sandbox with hardware IO and hot reload for ESP32 devices.

Outrun provides a sandboxed Lua runtime that can be loaded with new code over the network at any time. Hardware peripherals are exposed to Lua through a driver interface, so apps can draw to displays, read sensors, and control outputs without touching C++.

## Quick Start

### With `Outrun::Device` (network-connected)

`Outrun::Device` composes [Courier](https://github.com/inanimate-tech/courier) for connectivity with the sandbox runtime. It handles WiFi, WebSocket transport, and message routing automatically.

```cpp
#include <OutrunDevice.h>
#include "MyDisplayDriver.h"
#include "MyButtonDriver.h"

MyDisplayDriver display;
MyButtonDriver button{...};   // however your driver takes config

Outrun::DeviceConfig makeConfig() {
    Outrun::DeviceConfig cfg;
    cfg.deviceType    = "demo";
    cfg.host          = "your-server.example.com";
    cfg.statusDisplay = &display;
    cfg.extensions    = {&display, &button};
    return cfg;
}

class MyDevice : public Outrun::Device {
public:
    MyDevice() : Outrun::Device(makeConfig()) {}
};

MyDevice device;

void setup() { device.setup(); }
void loop()  { device.loop(); }
```

The device connects to WiFi (with a captive portal for configuration), opens a WebSocket to your server, and accepts Lua apps and shader expressions as JSON messages.

For telemetry callback or timezone, call `sandbox().setTelemetryCallback(...)` / `sandbox().setTimezone(...)` from your `deviceSetup()` override.

### Standalone `Outrun::Sandbox` (no network)

Use the sandbox directly without any network stack:

```cpp
#include <OutrunSandbox.h>
#include "MyLEDDriver.h"

MyLEDDriver led;

Outrun::SandboxConfig makeConfig() {
    Outrun::SandboxConfig cfg;
    cfg.extensions = {&led};
    return cfg;
}

Outrun::Sandbox sandbox{makeConfig()};

void setup() {
    sandbox.initialize();
    sandbox.loadApp(
        "function on_tick(ctx, dt_ms)\n"
        "  local t = ctx.time_ms / 1000\n"
        "  led.set_rgb(math.sin(t)*127+128, 0, 0)\n"
        "end\n"
    );
}

void loop() {
    sandbox.loop();
}
```

## Writing a Driver

Drivers expose hardware to Lua via a builder API:

```cpp
#include <OutrunDriver.h>
#include <OutrunLuaModule.h>
#include <M5Unified.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

class IMUDriver : public Outrun::Driver {
public:
    const char* name() const override { return "imu"; }
    void registerModule(Outrun::LuaModule& m) override {
        m.method<&IMUDriver::accel>("accel");
    }

    int accel(lua_State* L) {
        M5.Imu.update();
        auto d = M5.Imu.getImuData();
        lua_pushnumber(L, d.accel.x);
        lua_pushnumber(L, d.accel.y);
        lua_pushnumber(L, d.accel.z);
        return 3;
    }
};
```

Then in Lua:

```lua
function on_tick(ctx, dt_ms)
    local ax, ay, az = imu.accel()
    -- use acceleration data
end
```

For Lua-only extensions that don't expose hardware or emit events, extend `Outrun::Extension` directly instead of `Outrun::Driver` — the same `registerModule(LuaModule&)` and lifecycle hooks apply.

### Driver lifecycle

- `begin()` — called once by `Sandbox::initialize()` in registration order.
  Hardware init goes here. Idempotent: a manual early call is safe (the
  Sandbox's call becomes a no-op).
- `update()` — called every iteration of `Sandbox::loop()`. Use for
  per-tick driver work like polling and debouncing. Runs at full main-loop
  rate, distinct from Lua's 10 FPS `on_tick`.
- `registerModule(LuaModule& m)` — called once by `Sandbox::initialize()`
  to register the driver's Lua-visible global. Use the builder's
  `method<>`, `staticMethod`, and `constant` overloads.
- `onAppReset()` — called when a new app is loaded (before compilation).
- `onAppRunning(bool)` — called when an app starts or stops running.

## Message Protocol

When using `Outrun::Device`, the base `onMessage()` routes these types to the sandbox:

Subclasses can override `onMessage()` to handle custom types, then call `Device::onMessage()` for sandbox routing. The same pattern applies to `onConnected()`, `onConnectionChange()`, and `onTransportsWillConnect()` — override and call super.

Incoming JSON messages with these types are handled by the sandbox:

```json
{ "type": "app", "code": "function on_tick(ctx, dt_ms) ... end" }
{ "type": "shader", "expr": "rgb(sin(time_ms/1000)*0.5+0.5, 0, 0)" }
{ "type": "app_event", "name": "button_press", "data": { "id": 1 } }
```

### Lua app callbacks

- `init(ctx)` — called once after compilation
- `on_tick(ctx, dt_ms)` — called at 10 FPS with elapsed time
- `on_event(ctx, event)` — called for app_event messages and driver events

The `ctx` table contains: `time_ms`, `trigger_count`, `utc_h`, `utc_m`, `localtime_h`, `localtime_m`.

`localtime_h` / `localtime_m` return local time when a timezone has been set on the sandbox via `Sandbox::setTimezone(ianaZone)` and ezTime recognised the zone; otherwise they equal `utc_h` / `utc_m`.

### Shader expressions

Shader messages are converted to Lua via a template function you provide. The expression has access to `time_ms`, `trigger_count`, and time variables. Built-in helpers: `rgb(r,g,b)`, `fract(x)`, `beat(bpm)`, `noise2d(x,y)`.

### Timezone

`Sandbox::setTimezone(const char* ianaZone)` — set the sandbox's local timezone for `ctx.localtime_h/m` and the `time.*` Lua bindings. Pass an IANA zone string (e.g. `"Europe/London"`). ezTime performs a UDP lookup to `timezoned.rop.nl` on first sight of a zone and caches the POSIX string in EEPROM. On failure (null / empty / unrecognised zone), the sandbox falls back to UTC.

`Sandbox::hasTimezone() const` — returns `true` after a successful `setTimezone`. Exposed to Lua as `time.has_timezone()`. When false, `time.hour()` / `time.minute()` / `time.second()` return UTC.

## Building

### PlatformIO

```ini
[env:dev]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
build_unflags = -std=gnu++11
build_flags   = -std=gnu++17
lib_deps =
    https://github.com/inanimate-tech/outrun.git
    https://github.com/inanimate-tech/courier.git
    tzapu/WiFiManager@^2.0.17
    bblanchon/ArduinoJson@^7.4.2
    ropg/ezTime@^0.8.3
    fischer-simon/Esp32Lua@^5.4.7
```

The `-std=gnu++17` flag is required — Outrun's `LuaModule` builder uses `template<auto>` (a C++17 feature). Arduino-ESP32 defaults to `-std=gnu++11`, hence the unflag/flag pair.

### ESP-IDF (Arduino as component)

Add to your `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS ../vendor)
```

And in `idf_component.yml`:

```yaml
dependencies:
  inanimate/outrun:
    version: "^0.1.0"
```

## License

[PolyForm Noncommercial 1.0.0](LICENSE)
