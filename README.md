# Outrun

Sandbox with hardware IO and hot reload for ESP32 devices.

Outrun provides a sandboxed Lua runtime that can be loaded with new code over the network at any time. Hardware peripherals are exposed to Lua through a driver interface, so apps can draw to displays, read sensors, and control outputs without touching C++.

## Quick Start

### With `Outrun::Device` (network-connected)

`Outrun::Device` composes [Courier](https://github.com/inanimate-tech/courier) for connectivity with the sandbox runtime. It handles WiFi, WebSocket transport, and message routing automatically.

```cpp
#include <OutrunDevice.h>
#include "MyDisplayDriver.h"

MyDisplayDriver display;

Outrun::DeviceConfig makeConfig() {
    Outrun::DeviceConfig cfg;
    cfg.deviceType = "demo";
    cfg.host = "your-server.example.com";
    cfg.statusDisplay = &display;
    return cfg;
}

class MyDevice : public Outrun::Device {
public:
    MyDevice() : Outrun::Device(makeConfig()) {}

    void deviceSetup() override {
        sandbox().addDriver(&display);
        sandbox().initialize();
    }

    void deviceLoop() override {
        sandbox().loop();
    }
};

MyDevice device;

void setup() { device.setup(); }
void loop()  { device.loop(); }
```

The device connects to WiFi (with a captive portal for configuration), opens a WebSocket to your server, and accepts Lua apps and shader expressions as JSON messages.

### Standalone `Outrun::Sandbox` (no network)

Use the sandbox directly without any network stack:

```cpp
#include <OutrunSandbox.h>
#include "MyLEDDriver.h"

MyLEDDriver led;
Outrun::Sandbox sandbox;

void setup() {
    sandbox.addDriver(&led);
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

Drivers expose hardware to Lua by registering global functions:

```cpp
#include <OutrunDriver.h>
#include <M5Unified.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

class IMUDriver : public Outrun::Driver {
public:
    const char* name() const override { return "imu"; }

    void installSandboxModule(lua_State* L) override {
        lua_newtable(L);
        lua_pushcfunction(L, lua_accel);
        lua_setfield(L, -2, "accel");
        lua_setglobal(L, "imu");
    }

private:
    static int lua_accel(lua_State* L) {
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

### Driver lifecycle

- `installSandboxModule(L)` — called once during `sandbox.initialize()` to register Lua globals
- `onAppReset()` — called when a new app is loaded, before compilation
- `onAppRunning(bool)` — called when an app starts or stops running

## Message Protocol

When using `Outrun::Device`, incoming JSON messages with these types are routed to the sandbox automatically:

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

### Shader expressions

Shader messages are converted to Lua via a template function you provide. The expression has access to `time_ms`, `trigger_count`, and time variables. Built-in helpers: `rgb(r,g,b)`, `fract(x)`, `beat(bpm)`, `noise2d(x,y)`.

## Building

### PlatformIO

```ini
[env:dev]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
lib_deps =
    https://github.com/inanimate-tech/outrun.git
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
  inanimate/outrun:
    version: "^0.1.0"
```

## License

[PolyForm Noncommercial 1.0.0](LICENSE)
