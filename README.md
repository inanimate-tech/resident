# Resident

Sandbox with hardware IO and hot reload for ESP32 devices.

Resident provides a sandboxed Lua runtime that can be loaded with new code over the network at any time. Hardware peripherals are exposed to Lua through a driver interface, so apps can draw to displays, read sensors, and control outputs without touching C++.

## What Resident includes

1. The Resident firmware library for a sandbox on ESP32 devices and custom hardware integration, with optional managed connectivity to load sandbox apps remotely.
2. A default websocket backend at `resident.inanimate.tech/devices/<deviceId>` to relay apps and events during development.
3. Agent skills to create, validate and push sandbox apps. [Install the Resident skills plug-in](tools/agent-plugin/README.md).

Point your agent at [docs/start-building.md](docs/start-building.md) to add the Resident sandbox to your hardware.

## Examples

Working PlatformIO projects for specific boards live under [examples/](examples/) — currently the M5StickC Plus2 and the Adafruit ESP32-S2 TFT Feather. Each is buildable as-is; use them as templates for bringing up your own hardware.

### With `Resident::Device` (network-connected)

`Resident::Device` composes [Courier](https://github.com/inanimate-tech/courier) for connectivity with the sandbox runtime. It handles WiFi, WebSocket transport, and message routing automatically.

```cpp
#include <ResidentDevice.h>
#include "MyDisplayDriver.h"
#include "MyButtonDriver.h"

MyDisplayDriver display;
MyButtonDriver button{...};   // however your driver takes config

Resident::DeviceConfig makeConfig() {
    Resident::DeviceConfig cfg;
    cfg.deviceType    = "demo";
    cfg.host          = "your-server.example.com";
    cfg.statusDisplay = &display;
    cfg.extensions    = {&display, &button};
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

The device connects to WiFi (with a captive portal for configuration), opens a WebSocket to your server, and accepts Lua apps and shader expressions as JSON messages.

For telemetry callback or timezone, call `sandbox().setTelemetryCallback(...)` / `sandbox().setTimezone(...)` from your `deviceSetup()` override.

### Standalone `Resident::Sandbox` (no network)

Use the sandbox directly without any network stack:

```cpp
#include <ResidentSandbox.h>
#include "MyLEDDriver.h"

MyLEDDriver led;

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.extensions = {&led};
    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

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
#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <M5Unified.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

class IMUDriver : public Resident::Driver {
public:
    const char* name() const override { return "imu"; }
    void registerModule(Resident::LuaModule& m) override {
        m.method<IMUDriver, &IMUDriver::accel>("accel");
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

For Lua-only extensions that don't expose hardware or emit events, extend `Resident::Extension` directly instead of `Resident::Driver` — the same `registerModule(LuaModule&)` and lifecycle hooks apply.

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

When using `Resident::Device`, the base `onMessage()` routes these types to the sandbox:

Subclasses can override `onMessage()` to handle custom types, then call `Device::onMessage()` for sandbox routing. The same pattern applies to `onConnected()`, `onConnectionChange()`, and `onTransportsWillConnect()` — override and call super.

Incoming JSON messages with these types are handled by the sandbox:

```json
{ "type": "app", "code": "function on_tick(ctx, dt_ms) ... end" }
{ "type": "shader", "expr": "rgb(sin(time_ms/1000)*0.5+0.5, 0, 0)" }
{ "type": "app_event", "name": "button_press", "data": { "id": 1 } }
```

### Sandbox lifecycle

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
lib_deps =
    https://github.com/inanimate-tech/resident.git
    https://github.com/inanimate-tech/courier.git
    tzapu/WiFiManager@^2.0.17
    bblanchon/ArduinoJson@^7.4.2
    ropg/ezTime@^0.8.3
    fischer-simon/Esp32Lua@^5.4.7
```

### ESP-IDF (Arduino as component)

Add to your `CMakeLists.txt`:

```cmake
set(EXTRA_COMPONENT_DIRS ../vendor)
```

And in `idf_component.yml`:

```yaml
dependencies:
  inanimate/resident:
    version: "^0.1.0"
```

## License

[MIT](LICENSE)
