# Migrating from Resident 0.4.x to 0.5.0

0.5.0 is a coordinated breaking release: `Resident::Device` is gone, replaced by an expanded `Resident::Sandbox` that you instantiate (no subclassing) and configure with callback registrations. This guide walks through every change you'll see in your existing `main.cpp`.

If your project doesn't use Resident's network-attached features at all (you only ever instantiated `Resident::Sandbox` standalone, never `Resident::Device`), the only change you need is the `cfg.network` field shape — see "Standalone-only projects" at the end.

## Header includes

| Old | New |
|---|---|
| `#include <ResidentDevice.h>` | `#include <Resident.h>` (umbrella) or `#include <ResidentSandbox.h>` |
| `#include <ResidentDeviceConfig.h>` | removed; use `<ResidentSandboxConfig.h>` (in the umbrella) |
| `#include <ResidentSandbox.h>` | unchanged |

## The shift in one sentence

You no longer subclass anything. You construct one `Resident::Sandbox` at file scope, register callbacks against it from `setup()`, and call `sandbox.setup()` and `sandbox.loop()`. This matches Courier's idiom: instantiate, register, run.

## `Resident::Device` → `Resident::Sandbox`

| Old | New |
|---|---|
| `class FooDevice : public Resident::Device { ... };` | (removed — no subclassing) |
| `Resident::Device` (the class) | `Resident::Sandbox` (one class, networking is config) |
| `Resident::DeviceConfig` (the struct) | `Resident::SandboxConfig` |
| `device.setup()` / `device.loop()` | `sandbox.setup()` / `sandbox.loop()` |
| `device.sandbox().loadApp(code)` | `sandbox.loadApp(code)` (you ARE the sandbox) |
| `device.courier()` | `sandbox.courier()` |
| `device.getDeviceId()` | `sandbox.getDeviceId()` |
| `device.isConnected()` | `sandbox.isConnected()` |
| Subclass `protected: Courier::WebSocketTransport& _ws;` | Public `sandbox.ws()` accessor |

## Config field migration

`DeviceConfig` and `SandboxConfig` are merged. Most fields keep their name; network-related ones nest into `cfg.network`.

| Old (`DeviceConfig`) | New (`SandboxConfig`) |
|---|---|
| `cfg.deviceType` | `cfg.deviceType` (unchanged, top-level) |
| `cfg.host` | `cfg.network->host` |
| `cfg.dns1`, `cfg.dns2` | `cfg.network->dns1`, `cfg.network->dns2` |
| `cfg.statusDisplay` | `cfg.statusDisplay` (unchanged, top-level) |
| `cfg.statusLED` | `cfg.statusLED` (unchanged, top-level) |
| `cfg.extensions` | `cfg.extensions` (unchanged, top-level) |
| `cfg.shaderTemplate` | `cfg.shaderTemplate` (unchanged, top-level) |
| (was on `SandboxConfig`) `cfg.telemetry` | `cfg.telemetry` (unchanged) |
| (was on `SandboxConfig`) `cfg.timezone` | `cfg.timezone` (unchanged) |

`cfg.network` is `std::optional<Courier::Config>`. **Setting it is what makes a Sandbox network-attached.** Omit it for a standalone Lua runtime that pulls in no WiFi.

Old:
```cpp
Resident::DeviceConfig cfg;
cfg.deviceType    = "feather-tft";
cfg.host          = "resident.inanimate.tech";
cfg.statusDisplay = &display;
cfg.statusLED     = &led;
cfg.extensions    = {&display, &led, &battery};
```

New:
```cpp
Resident::SandboxConfig cfg;
cfg.deviceType    = "feather-tft";
cfg.statusDisplay = &display;
cfg.statusLED     = &led;
cfg.extensions    = {&display, &led, &battery};

Courier::Config courier;
courier.host = "resident.inanimate.tech";
// courier.dns1 = ..., courier.port = ..., etc. — anything Courier::Config takes
cfg.network = courier;
```

### `Courier::Config` syntax: field assignment, not designated initializers

This is the one syntax gotcha worth calling out separately because it tends to bite during migration. **Always construct `Courier::Config` with the default constructor and then assign fields — do not use designated initializers (`Courier::Config{ .host = "..." }`).**

Right:
```cpp
Courier::Config courier;
courier.host = "resident.inanimate.tech";
courier.port = 443;
cfg.network  = courier;
```

Wrong (compiles under PlatformIO, breaks under ESP-IDF):
```cpp
// DO NOT WRITE THIS:
// cfg.network = Courier::Config{ .host = "resident.inanimate.tech" };
```

Why: `Courier::Config` has a constructor with default arguments, which makes it a non-aggregate type. C++20 designated initializers are only allowed on aggregates. GCC accepts the construct as an extension under PlatformIO's `-std=gnu++17` build, but ESP-IDF's `-std=gnu++2b -Werror` rejects it, and the diagnostic is a confusing "could not convert" error rather than something that obviously points at the syntax.

The field-assignment pattern is one line longer and works under every Resident-supported build. The four shipped example projects all use it; copy from `examples/m5stick-demo/device/src/main.cpp` if in doubt.

## Subclass virtuals → callback registration

Every override on `Resident::Device` becomes a callback on `Resident::Sandbox`. Single-slot, last registration wins, register before `sandbox.setup()`.

### `onTransportsWillConnect`

Old (subclass override; called `_ws` member directly):
```cpp
class FeatherDevice : public Resident::Device {
  void onTransportsWillConnect() override {
    String wsPath = String("/devices/") + getDeviceId();
    _ws.setEndpoint(HOST, PORT, wsPath.c_str());
  }
};
```

New (callback registration; uses public `sandbox.ws()` accessor):
```cpp
sandbox.onTransportsWillConnect([]() {
  String wsPath = String("/devices/") + sandbox.getDeviceId();
  sandbox.ws().setEndpoint(HOST, PORT, wsPath.c_str());
});
```

The default Resident WS path (`/agents/<deviceType>-agent/<deviceId>`) still fires before your callback runs; setting the endpoint inside your callback overrides it. No change in semantics — just a different way to express the override.

### `onConnected`

Old:
```cpp
class FooDevice : public Resident::Device {
  void onConnected() override {
    // post-connection work
  }
};
```

New:
```cpp
sandbox.onConnected([]() {
  // post-connection work
});
```

**Note:** `onConnected` fires every time the connection state transitions to `Connected`, including after reconnects. If your old code used a `static bool loaded = false` guard inside `deviceLoop()` to do a one-shot bootstrap, preserve that guard inside the callback (otherwise reconnects will re-run the bootstrap and clobber any pushed app):

```cpp
sandbox.onConnected([]() {
  static bool loaded = false;
  if (loaded) return;
  loaded = true;
  // one-shot bootstrap work here
});
```

### `onConnectionChange`

Old:
```cpp
class FooDevice : public Resident::Device {
  void onConnectionChange(Courier::State state) override {
    Resident::Device::onConnectionChange(state);   // keep default behavior
    // your own state-change work
  }
};
```

New:
```cpp
sandbox.onConnectionChange([](Courier::State state) {
  // your own state-change work
});
```

**No super call.** Resident's internal handlers (status text, status LED color) run *before* your callback unconditionally — you don't have to opt in to them by calling super. Setting `cfg.statusDisplay = nullptr` / `cfg.statusLED = nullptr` opts out of the defaults.

### `onMessage`

Old (subclass override + super call to keep sandbox routing):
```cpp
class FooDevice : public Resident::Device {
  void onMessage(const char* transportName, const char* type,
                  JsonDocument& doc) override {
    if (strcmp(type, "my-custom-type") == 0) {
      handleMyCustomType(doc);
      return;
    }
    Resident::Device::onMessage(transportName, type, doc);  // keep app/shader/event routing
  }
};
```

New (callback; reserved types routed internally, your callback fires only for the rest):
```cpp
sandbox.onMessage([](const char* transportName, const char* type,
                      JsonDocument& doc) {
  if (strcmp(type, "my-custom-type") == 0) {
    handleMyCustomType(doc);
  }
  // app/shader/app_event are handled by Resident before your callback runs
  // — no super call to make, no early-return needed.
});
```

This is the most common gotcha during migration: **delete the super call, and delete any `if (type == "app" / "shader" / "app_event")` branches** — Resident now handles those internally, your callback never sees them.

### `deviceSetup()` and `deviceLoop()`

These hooks are gone. Put their bodies directly in Arduino's `setup()`/`loop()`, around the calls to `sandbox.setup()`/`sandbox.loop()`.

Old:
```cpp
class FooDevice : public Resident::Device {
  void deviceSetup() override {
    // additional setup work that happens after Resident's wiring
    // but before connection starts
    M5.update();
  }
  void deviceLoop() override {
    // additional per-tick work after Resident's loop work
    static bool loaded = false;
    if (!loaded && isConnected()) {
      loaded = true;
      sandbox().loadApp(...);
    }
  }
};
FooDevice device;
void setup() { device.setup(); }
void loop()  { device.loop(); }
```

New:
```cpp
Resident::Sandbox sandbox{cfg};

void setup() {
  // your old deviceSetup() body — but most of what you had in deviceSetup
  // probably fits better in onConfigureNetwork (for Courier work) or
  // onConnected (for the bootstrap-app pattern below).
  sandbox.onConnected([]() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    sandbox.loadApp(...);
  });

  sandbox.setup();
}

void loop() {
  sandbox.loop();
  // your old deviceLoop() body, except connection-state-watching belongs
  // in onConnected/onConnectionChange callbacks.
}
```

The translation isn't always a straight 1:1; treat it as an opportunity to move state-aware work into the matching callback.

## TLS certs, custom headers, custom WiFi config: `onConfigureNetwork`

Old: pass-through fields on `DeviceConfig` (`dns1`, `dns2`) and no clean path for a TLS cert at all — you'd have to reach into `device.courier().transport<Courier::WebSocketTransport>("ws")` somewhere ad-hoc.

New: one hook, fired once at the right lifecycle moment (Courier constructed, transports not yet started):
```cpp
sandbox.onConfigureNetwork([](Courier::Client& c) {
  // TLS cert on the built-in WS transport
  c.transport<Courier::WebSocketTransport>("ws").onConfigure(
    [](esp_websocket_client_config_t& ws) { ws.cert_pem = MY_CERT; }
  );

  // Or: register an additional transport
  // c.addTransport<Courier::MqttTransport>("mqtt", mqttCfg);

  // Or: WiFi-config callback (custom WiFiManager params, hostname, etc.)
  // c.onConfigureWiFi([](WiFiManager& wm) { ... });
});
```

The arg is `Courier::Client&` directly — no Resident wrapper, no abstraction layer. New Courier features become available as soon as Courier ships them.

## Accessors that moved or changed

| Old | New |
|---|---|
| `_ws` (protected member) | `sandbox.ws()` (public; asserts if standalone) |
| `device.courier()` | `sandbox.courier()` (asserts if standalone) |
| `device.sandbox()` | gone — `sandbox` IS the sandbox; call methods directly |
| `device.getDeviceId()` | `sandbox.getDeviceId()` |
| `device.getDeviceType()` | `sandbox.getDeviceType()` |
| `device.isConnected()` | `sandbox.isConnected()` |
| `device.isTimeSynced()` | `sandbox.isTimeSynced()` |
| (none) | `sandbox.hasNetwork()` — new; true iff `cfg.network` was set |

`courier()` and `ws()` assert if you call them on a standalone Sandbox (one constructed without `cfg.network`). This is a programming error, not a recoverable condition — gate callers on `sandbox.hasNetwork()` if uncertain.

## Standalone-only projects

If your project never used `Resident::Device`, the changes are minimal:

- Header `<ResidentSandboxConfig.h>` now also pulls in Courier headers (`Courier::Config` is referenced by the `cfg.network` field). Your build needs Courier in `lib_deps`. If you didn't have it before — you do now, even though you don't use it. The compiled binary doesn't pay for it (the optional is empty), but the dependency is mandatory.
- `cfg.network` field exists; leave it unset (default-constructed `std::optional` is empty).
- `Sandbox::initialize()` and `Sandbox::loop()` semantics are unchanged for the standalone path. `Sandbox::setup()` is new — call it instead of (or before) `initialize()`. Calling `setup()` is the recommended public entry point even standalone; it's idempotent and a no-op for the network parts when `cfg.network` is empty.

## A complete before/after

Old (`main.cpp`):
```cpp
#include <Arduino.h>
#include <ResidentDevice.h>
#include "DisplayDriver.h"

DisplayDriver display;

Resident::DeviceConfig makeConfig() {
  Resident::DeviceConfig cfg;
  cfg.deviceType    = "stick";
  cfg.host          = "resident.inanimate.tech";
  cfg.statusDisplay = &display;
  cfg.extensions    = {&display};
  return cfg;
}

class StickDevice : public Resident::Device {
public:
  StickDevice() : Resident::Device(makeConfig()) {}
  void onTransportsWillConnect() override {
    String p = String("/devices/") + getDeviceId();
    _ws.setEndpoint("resident.inanimate.tech", 443, p.c_str());
  }
  void deviceLoop() override {
    static bool loaded = false;
    if (!loaded && isConnected()) {
      loaded = true;
      String app = "function init(ctx) ... end";
      sandbox().loadApp(app.c_str());
    }
  }
};

StickDevice device;
void setup() { device.setup(); }
void loop()  { device.loop(); }
```

New:
```cpp
#include <Arduino.h>
#include <Resident.h>
#include "DisplayDriver.h"

DisplayDriver display;

Resident::SandboxConfig makeConfig() {
  Resident::SandboxConfig cfg;
  cfg.deviceType    = "stick";
  cfg.statusDisplay = &display;
  cfg.extensions    = {&display};

  Courier::Config courier;
  courier.host = "resident.inanimate.tech";
  cfg.network  = courier;

  return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
  sandbox.onTransportsWillConnect([]() {
    String p = String("/devices/") + sandbox.getDeviceId();
    sandbox.ws().setEndpoint("resident.inanimate.tech", 443, p.c_str());
  });
  sandbox.onConnected([]() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    String app = "function init(ctx) ... end";
    sandbox.loadApp(app.c_str());
  });
  sandbox.setup();
}

void loop() { sandbox.loop(); }
```

Net difference: a few lines shorter, the subclass is gone, and the bootstrap-app pattern is now expressed against the right lifecycle hook (`onConnected`) rather than polled in a `deviceLoop()`.

## What didn't change

- `Resident::Driver` and `Resident::Extension` base classes, the `LuaModule` builder, the `begin()` / `update()` / `registerModule()` / `onAppReset()` / `onAppRunning()` lifecycle. Driver authoring is unchanged.
- `Resident::StatusDisplay` and `Resident::StatusLED` interfaces.
- The wire protocol (`/devices/<id>` WS path, `app` / `shader` / `app_event` JSON shapes, device IDs).
- The Lua runtime (`init(ctx)`, `on_tick(ctx, dt_ms)`, `on_event(ctx, evt)`, `ctx` table shape, the `log` / `time` / `kv` / shader globals, 10 FPS tick rate).
- Physical-board vocabulary: `device/` example folders, `DEVICE-SKILL.md`, `--device-id` CLI flag, `cfg.deviceType`, `/resident:write-device-skill` skill name. "Device" stays as the consistent word for the physical board on the network.
- The agent skills (`/resident:create-app`, `/resident:push-app`, `/resident:validate-app`, `/resident:write-device-skill`).

## Mechanical translation checklist

For each file you migrate:

1. Replace `#include <ResidentDevice.h>` with `#include <Resident.h>`.
2. Delete the `class FooDevice : public Resident::Device { ... };` block; lift its overrides into callback registrations in `setup()`.
3. Rename the global instance: `FooDevice device` → `Resident::Sandbox sandbox{makeConfig()}`.
4. Replace `Resident::DeviceConfig` with `Resident::SandboxConfig`. Move `host`/`port`/`dns1`/`dns2` (and any other Courier fields) into `cfg.network` — but use direct field assignment, not designated initializers. See "`Courier::Config` syntax" above.
5. In `setup()`/`loop()`, replace `device.setup()`/`device.loop()` with `sandbox.setup()`/`sandbox.loop()`.
6. Inside any `onConnected` / bootstrap-app callback, add a `static bool loaded = false; if (loaded) return; loaded = true;` guard if you want one-shot semantics.
7. Inside any old `onMessage` override: delete the `Resident::Device::onMessage(...)` super call, and delete any `if (type == "app" / "shader" / "app_event")` branches — Resident handles those internally now.
8. Any reach into `_ws` becomes `sandbox.ws()`. Any reach into `courier()` keeps its name.
9. Build. Cppcheck/compile errors will surface anything missed; signature mismatches in `onMessage` are the most common.
