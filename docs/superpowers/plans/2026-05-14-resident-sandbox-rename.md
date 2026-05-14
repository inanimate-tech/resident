# Resident::Sandbox rename + callback API — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Collapse `Resident::Device` into `Resident::Sandbox` so the C++ surface matches the landing-page story; replace virtual subclass hooks with callback registration to align with Courier's idiom; embed `Courier::Config` directly in `cfg.network` so new Courier knobs no longer need a Resident pass-through.

**Architecture:** One public `Resident::Sandbox` class (no subclassing). Networking is opt-in via `cfg.network = std::optional<Courier::Config>`. All extension points are `sandbox.on*(cb)` registrations (`onConfigureNetwork`, `onTransportsWillConnect`, `onMessage`, `onConnectionChange`, `onConnected`). Resident's internal handlers for status indicators and message routing run *alongside* user callbacks (not as user-overridable virtuals). `cfg.statusDisplay`/`cfg.statusLED` move to top level of `SandboxConfig`. Headers consolidated: `Resident.h` + `ResidentSandbox.h` (no more `ResidentDevice.h` / `ResidentDeviceConfig.h`).

**Tech Stack:** C++17 (gnu++17), Lua 5.4 (Esp32Lua), Courier (WiFi + WebSocket), PlatformIO, ESP-IDF, Unity test framework on `native`, cppcheck.

**Spec:** `docs/superpowers/specs/2026-05-14-resident-sandbox-rename-design.md`

**Branch:** `sandbox-rename` (branched from main).

**Hardware-verification gate:** Per repo convention, every commit that touches `examples/*/src/` or `examples/*/platformio.ini` must wait for Matt's on-hardware verification before being committed. Tasks 6, 7, and 8 each end at "build clean locally; pause for hardware verification; then commit." Do not collapse these gates. (Task 9, the espidf-basic example, targets `example.com` and never actually connects to a real network — the CI build is the verification, no hardware required.)

---

## Phase overview

| Phase | Tasks | Outcome |
|---|---|---|
| 1. Library surface | 1–5 | New `Resident::Sandbox` public API in place; old `Resident::Device` still present, side-by-side |
| 2. Example migrations | 6–9 | Each example moved to the new API; the three with hardware (Tasks 6–8) gated on hardware verification |
| 3. Old API removal | 10 | `ResidentDevice.{h,cpp,Config.h}` deleted; CMake updated; all builds green |
| 4. CI expansion | 11 | `tools/run-tests.py build` covers all PlatformIO example projects |
| 5. Docs + version | 12–15 | README, start-building, api, example READMEs, write-device-skill SKILL.md, **migration guide for downstream agents**, changelog, version bump |
| 6. Final sweep | 16 | Grep for residual references; fix any stragglers |

---

## Task 1: Add new fields to `SandboxConfig`

Add `deviceType`, `statusDisplay`, `statusLED`, and `network` (optional `Courier::Config`) to `SandboxConfig` so it can absorb everything `DeviceConfig` carries today. Keep existing `extensions`, `shaderTemplate`, `telemetry`, `timezone` fields unchanged.

**Files:**
- Modify: `src/ResidentSandboxConfig.h`
- Modify: `test/unit/test/test_extensions/test_extensions.cpp` (add `network`/status field assertions)

- [ ] **Step 1: Read current SandboxConfig**

Run: `cat src/ResidentSandboxConfig.h`

Confirm it currently has only `extensions`, `shaderTemplate`, `telemetry`, `timezone`.

- [ ] **Step 2: Write the failing test**

Add a new test file `test/unit/test/test_sandbox_config/test_sandbox_config.cpp`:

```cpp
#include <unity.h>
#include "ResidentSandboxConfig.h"
#include "ResidentExtension.h"

namespace {
class StubExt : public Resident::Extension {
public:
  const char* name() const override { return "stub"; }
};
class StubStatusDisplay : public Resident::StatusDisplay {
public:
  void displayText(const char* text) override { (void)text; }
};
class StubStatusLED : public Resident::StatusLED {
public:
  void solidColor(uint32_t rgb) override { (void)rgb; }
};
}

void setUp(void) {}
void tearDown(void) {}

void test_default_construction(void) {
    Resident::SandboxConfig cfg;
    TEST_ASSERT_NULL(cfg.deviceType);
    TEST_ASSERT_NULL(cfg.statusDisplay);
    TEST_ASSERT_NULL(cfg.statusLED);
    TEST_ASSERT_FALSE(cfg.network.has_value());
    TEST_ASSERT_EQUAL_INT(0, (int)cfg.extensions.count);
    TEST_ASSERT_NULL(cfg.shaderTemplate);
    TEST_ASSERT_NULL(cfg.timezone);
}

void test_assign_top_level_fields(void) {
    StubExt e;
    StubStatusDisplay sd;
    StubStatusLED sl;

    Resident::SandboxConfig cfg;
    cfg.deviceType    = "feather-tft";
    cfg.extensions    = {&e};
    cfg.statusDisplay = &sd;
    cfg.statusLED     = &sl;
    cfg.timezone      = "Europe/London";

    TEST_ASSERT_EQUAL_STRING("feather-tft", cfg.deviceType);
    TEST_ASSERT_EQUAL_INT(1, (int)cfg.extensions.count);
    TEST_ASSERT_EQUAL_PTR(&sd, cfg.statusDisplay);
    TEST_ASSERT_EQUAL_PTR(&sl, cfg.statusLED);
    TEST_ASSERT_EQUAL_STRING("Europe/London", cfg.timezone);
}

void test_network_optional_can_be_assigned(void) {
    Resident::SandboxConfig cfg;
    Courier::Config c;
    c.host = "resident.inanimate.tech";
    c.dns1 = 0x08080808;
    cfg.network = c;

    TEST_ASSERT_TRUE(cfg.network.has_value());
    TEST_ASSERT_EQUAL_STRING("resident.inanimate.tech", cfg.network->host);
    TEST_ASSERT_EQUAL_UINT32(0x08080808, cfg.network->dns1);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_construction);
    RUN_TEST(test_assign_top_level_fields);
    RUN_TEST(test_network_optional_can_be_assigned);
    return UNITY_END();
}
```

- [ ] **Step 3: Add Courier to native test deps**

Edit `test/unit/platformio.ini`. Add `inanimate/courier` to `lib_deps` (Courier headers are needed to compile the new SandboxConfig that includes `Courier::Config`):

```ini
[env:native]
platform = native
test_framework = unity
lib_compat_mode = off
build_flags =
    -std=gnu++17
    -Wall
    -Iinclude
    -I../../src
lib_deps =
    fischer-simon/Esp32Lua@^5.4.7
    https://github.com/inanimate-tech/courier.git
```

- [ ] **Step 4: Run test to verify it fails**

Run: `./tools/run-tests.py unit`
Expected: FAIL — fields like `cfg.deviceType` and `cfg.network` don't exist on `SandboxConfig` yet, plus the `Courier::Config` `#include` is missing.

- [ ] **Step 5: Update SandboxConfig**

Replace `src/ResidentSandboxConfig.h` with:

```cpp
// src/ResidentSandboxConfig.h
#ifndef RESIDENT_SANDBOX_CONFIG_H
#define RESIDENT_SANDBOX_CONFIG_H

#include <Arduino.h>
#include <map>
#include <functional>
#include <optional>
#include <Courier.h>
#include "ResidentExtensions.h"
#include "ResidentStatusLED.h"
#include "ResidentStatusDisplay.h"

namespace Resident {

using ShaderFields = std::map<String, String>;
using ShaderTemplateFn = String (*)(const ShaderFields& fields);
using TelemetryCallback = std::function<void(const char* json)>;

struct SandboxConfig {
  // Identifies the physical board (used for AP name and protocol path
  // defaults). Stays at top-level — it labels the device, not the network.
  const char* deviceType = nullptr;

  // Hardware bindings exposed to Lua, plus shader-expression template.
  Extensions extensions;
  ShaderTemplateFn shaderTemplate = nullptr;

  // Lua-side telemetry sink + per-board IANA timezone.
  TelemetryCallback telemetry = nullptr;
  const char* timezone = nullptr;

  // Status indicators are properties of the *device*, not the network —
  // top-level so a future standalone use case can drive them too. Resident's
  // internal handlers update them on connection state changes when network
  // is configured.
  StatusDisplay* statusDisplay = nullptr;
  StatusLED* statusLED = nullptr;

  // Networking opt-in. Presence ⇒ Sandbox constructs a Courier::Client with
  // this config, drives WiFi/transports, fires onConnected/onMessage/etc.
  // Absence ⇒ standalone runtime, no WiFi pulled in.
  std::optional<Courier::Config> network;
};

} // namespace Resident

#endif // RESIDENT_SANDBOX_CONFIG_H
```

- [ ] **Step 6: Run test to verify it passes**

Run: `./tools/run-tests.py unit`
Expected: PASS — both the existing `test_extensions` tests and the new `test_sandbox_config` tests pass.

- [ ] **Step 7: Update test_extensions comment**

In `test/unit/test/test_extensions/test_extensions.cpp`, the comment in `test_reassignment_resets_all_slots` references "DeviceConfig". Update it:

```cpp
    // Production pattern: makeConfig() builds SandboxConfig fresh each call,
    // but tests/setup may reassign Extensions on an existing struct. Verify
```

- [ ] **Step 8: Commit**

```bash
git add src/ResidentSandboxConfig.h \
        test/unit/test/test_sandbox_config/test_sandbox_config.cpp \
        test/unit/test/test_extensions/test_extensions.cpp \
        test/unit/platformio.ini
git commit -m "feat(sandbox): add network/status/deviceType fields to SandboxConfig

Folds DeviceConfig's surface into SandboxConfig:
- deviceType (top-level; labels the physical board)
- statusDisplay, statusLED (top-level; properties of the device)
- network (std::optional<Courier::Config>; presence ⇒ networked)

Existing fields unchanged. Resident::Device still present and functional."
```

---

## Task 2: Add internal Courier client + accessors to `Sandbox`

Wire Courier into `Resident::Sandbox` as an `std::optional<Courier::Client>` member that's only constructed when `cfg.network` is set. Add `courier()` and `ws()` accessors. This task only adds members and accessors — `setup()`/`loop()` orchestration comes in Task 5.

**Files:**
- Modify: `src/ResidentSandbox.h`
- Modify: `src/ResidentSandbox.cpp`

- [ ] **Step 1: Add includes + private members to ResidentSandbox.h**

Edit `src/ResidentSandbox.h`. Add at the top (with other includes):

```cpp
#include <optional>
#include <Courier.h>
```

Add to the `class Sandbox { public: ... };` section, after the existing public methods:

```cpp
  // Network accessors. Both assert if cfg.network was not set.
  Courier::Client& courier();
  Courier::WebSocketTransport& ws();

  // True iff cfg.network was set at construction time.
  bool hasNetwork() const { return _courier.has_value(); }
```

Add to the private section (after the existing private members):

```cpp
  // Optional Courier client — constructed iff cfg.network was set at
  // construction time. WS transport reference is cached for ws() accessor.
  std::optional<Courier::Client> _courier;
  Courier::WebSocketTransport* _ws = nullptr;
  String _deviceId;
```

- [ ] **Step 2: Implement constructor wiring + accessors in ResidentSandbox.cpp**

Edit `src/ResidentSandbox.cpp`. Add `#include "chipstring.h"` near the top.

Update the `Sandbox::Sandbox(const SandboxConfig& config)` constructor body to construct Courier when network is set. The current constructor delegates to the no-arg constructor then calls `configure()`. Replace with:

```cpp
Sandbox::Sandbox(const SandboxConfig& config) : Sandbox() {
  configure(config);
  _deviceId = ::getDeviceId();

  if (_config.network.has_value()) {
    _courier.emplace(*_config.network);
    _ws = &_courier->transport<Courier::WebSocketTransport>("ws");
  }
}
```

Add accessor implementations (anywhere in the file's namespace block, e.g. after `setTimezone`):

```cpp
Courier::Client& Sandbox::courier() {
  // hasNetwork() == false would mean cfg.network was unset — programming
  // error; assert rather than return a dangling reference.
  assert(_courier.has_value());
  return *_courier;
}

Courier::WebSocketTransport& Sandbox::ws() {
  assert(_ws != nullptr);
  return *_ws;
}
```

Add `#include <cassert>` near the top if not already present.

- [ ] **Step 3: Build the m5stick-demo example to confirm headers compile**

Run: `cd examples/m5stick-demo/device && pio run`
Expected: PASS — Courier::Config inclusion in SandboxConfig doesn't break the existing Device-based example. Sandbox-side changes are additive only at this point.

(If this fails because Courier::Config isn't being found, ensure Courier is reachable via the existing `lib_deps`.)

- [ ] **Step 4: Run unit tests**

Run: `./tools/run-tests.py unit`
Expected: PASS — `test_sandbox_config` and `test_extensions` still pass.

- [ ] **Step 5: Commit**

```bash
git add src/ResidentSandbox.h src/ResidentSandbox.cpp
git commit -m "feat(sandbox): conditionally construct Courier client + add accessors

Sandbox now constructs std::optional<Courier::Client> when cfg.network is
set; ws() and courier() accessors expose it (assert if standalone).
Device-id derivation moved into Sandbox via chipstring.h. Sandbox::setup()
orchestration arrives in a later commit; Resident::Device still owns the
lifecycle for now."
```

---

## Task 3: Add callback registration API to `Sandbox`

Add `std::function` members for each extension point and matching `on*(cb)` setters. No firing logic yet — just storage.

**Files:**
- Modify: `src/ResidentSandbox.h`
- Modify: `src/ResidentSandbox.cpp`

- [ ] **Step 1: Add callback type aliases + setters to ResidentSandbox.h**

Edit the public section of `class Sandbox`:

```cpp
  // ── Setup-phase callback (register before setup()) ──
  using ConfigureNetworkCallback = std::function<void(Courier::Client&)>;
  void onConfigureNetwork(ConfigureNetworkCallback cb) {
    _onConfigureNetwork = std::move(cb);
  }

  // ── Reactive callbacks (single-slot, last registration wins) ──
  using TransportsWillConnectCallback = std::function<void()>;
  using MessageCallback = std::function<void(const char* transportName,
                                              const char* type,
                                              JsonDocument& doc)>;
  using ConnectionChangeCallback = std::function<void(Courier::State)>;
  using ConnectedCallback = std::function<void()>;

  void onTransportsWillConnect(TransportsWillConnectCallback cb) {
    _onTransportsWillConnect = std::move(cb);
  }
  void onMessage(MessageCallback cb) {
    _onMessage = std::move(cb);
  }
  void onConnectionChange(ConnectionChangeCallback cb) {
    _onConnectionChange = std::move(cb);
  }
  void onConnected(ConnectedCallback cb) {
    _onConnected = std::move(cb);
  }

  // Identity / status accessors
  const String& getDeviceId() const { return _deviceId; }
  const char* getDeviceType() const {
    return _config.deviceType ? _config.deviceType : "device";
  }
  bool isConnected() const;
  bool isTimeSynced() const;
```

Add to the private members:

```cpp
  // User-registered callbacks (single-slot, last registration wins).
  ConfigureNetworkCallback     _onConfigureNetwork;
  TransportsWillConnectCallback _onTransportsWillConnect;
  MessageCallback              _onMessage;
  ConnectionChangeCallback     _onConnectionChange;
  ConnectedCallback            _onConnected;
```

Add `#include <ArduinoJson.h>` near the top if not already present (the `MessageCallback` signature references `JsonDocument`).

- [ ] **Step 2: Implement isConnected/isTimeSynced in ResidentSandbox.cpp**

```cpp
bool Sandbox::isConnected() const {
  return _courier.has_value() &&
         _courier->getState() == Courier::State::Connected;
}

bool Sandbox::isTimeSynced() const {
  return _courier.has_value() && _courier->isTimeSynced();
}
```

- [ ] **Step 3: Run unit tests**

Run: `./tools/run-tests.py unit`
Expected: PASS — adding setters/state accessors is additive.

- [ ] **Step 4: Run static analysis**

Run: `./tools/run-tests.py static-analysis`
Expected: PASS — cppcheck on src/ shows no new warnings.

- [ ] **Step 5: Commit**

```bash
git add src/ResidentSandbox.h src/ResidentSandbox.cpp
git commit -m "feat(sandbox): add callback registration API + identity accessors

Adds onConfigureNetwork/onTransportsWillConnect/onMessage/
onConnectionChange/onConnected setters (single-slot, std::function-backed),
and isConnected/isTimeSynced/getDeviceId/getDeviceType accessors.
Setup/loop orchestration that fires these callbacks lands in the next commit."
```

---

## Task 4: Implement `Sandbox::setup()` and `Sandbox::loop()` orchestration

Wire the lifecycle. Standalone mode: `initialize()` only. Networked mode: fire `onConfigureNetwork`, set AP name, run extensions/initialize, then `_courier->setup()`. Hook Courier's callbacks to internal handlers (which delegate to user callbacks where applicable). Same pattern for loop.

**Files:**
- Modify: `src/ResidentSandbox.h` (add private setup helpers)
- Modify: `src/ResidentSandbox.cpp`

- [ ] **Step 1: Add private setup helpers to ResidentSandbox.h**

In the private section:

```cpp
  // Internal Courier hook handlers (drive status indicators + reserved-type
  // routing, then delegate to user callbacks).
  void wireInternalCourierHooks();
  void onCourierMessage(const char* transportName, const char* type,
                        JsonDocument& doc);
  void onCourierConnectionChange(Courier::State state);
  void onCourierConnected();
  void onCourierTransportsWillConnect();

  // Status display helper.
  void showStatusText(const char* text);
  String _lastStatusText;

  // Track whether the Lua state has been initialised, so setup() is idempotent.
  bool _initialized = false;

  // Setup wraps initialize(); rename existing public initialize() to private
  // since setup() is the public entry point now.
  // (initialize() stays public for tests + standalone use that wants a
  //  pre-loadApp() Lua state without going through full setup.)
```

Add these public methods to `Sandbox`:

```cpp
  // Public lifecycle — replaces the old Device::setup()/loop().
  // Standalone mode (no cfg.network): just initialises the Lua state.
  // Networked mode: also fires onConfigureNetwork, kicks off Courier.
  void setup();
  // loop() already exists for the standalone tick — extended to drive Courier.
```

(Note: `loop()` is already a public method; we're extending its behavior, not redeclaring.)

- [ ] **Step 2: Implement Sandbox::setup() in ResidentSandbox.cpp**

```cpp
void Sandbox::setup()
{
  if (_initialized) return;
  _initialized = true;

  if (_courier.has_value()) {
    // Re-derive deviceId in case it wasn't ready at construction.
    _deviceId = ::getDeviceId();

    // 1. User's onConfigureNetwork — first; lets them register transports,
    //    set certs, etc., before any Courier setup runs.
    if (_onConfigureNetwork) {
      _onConfigureNetwork(*_courier);
    }

    // 2. Wire Resident's internal handlers onto Courier. These run before
    //    user callbacks (status indicators, reserved-type routing) and then
    //    delegate.
    wireInternalCourierHooks();

    // 3. AP name for WiFi config portal.
    String apName = String(getDeviceType());
    if (apName.length() > 0) apName[0] = toupper(apName[0]);
    String idSuffix = _deviceId.substring(0, 4);
    _courier->setAPName(
      (String("Resident ") + apName + " " + idSuffix).c_str());

    Serial.printf("[resident] Device: %s (%s)\n",
                  getDeviceType(), _deviceId.c_str());
  }

  // 4. Status display starts up regardless of network.
  if (_config.statusDisplay) _config.statusDisplay->begin();

  // 5. Sandbox internals (Lua state, extensions). Always.
  initialize();

  // 6. Kick off Courier (WiFi + transports).
  if (_courier.has_value()) {
    _courier->setup();
  }
}
```

- [ ] **Step 3: Implement wireInternalCourierHooks()**

```cpp
void Sandbox::wireInternalCourierHooks()
{
  _courier->onMessage([this](const char* tn, const char* type, JsonDocument& d) {
    onCourierMessage(tn, type, d);
  });
  _courier->onConnectionChange([this](Courier::State s) {
    onCourierConnectionChange(s);
  });
  _courier->onTransportsWillConnect([this]() {
    onCourierTransportsWillConnect();
  });
  _courier->onConnected([this]() {
    onCourierConnected();
  });
}
```

- [ ] **Step 4: Implement onCourierMessage with reserved-type routing**

```cpp
void Sandbox::onCourierMessage(const char* transportName,
                                const char* type, JsonDocument& doc)
{
  // Reserved types — Resident handles internally; user callback never sees these.
  if (strcmp(type, "app") == 0) {
    const char* code = doc["code"];
    if (code) loadApp(code);
    return;
  }
  if (strcmp(type, "shader") == 0) {
    ShaderFields fields;
    for (JsonPair kv : doc.as<JsonObject>()) {
      if (strcmp(kv.key().c_str(), "type") == 0) continue;
      if (kv.value().is<const char*>()) {
        fields[String(kv.key().c_str())] = String(kv.value().as<const char*>());
      }
    }
    loadShader(fields);
    return;
  }
  if (strcmp(type, "app_event") == 0) {
    const char* name = doc["name"];
    char dataJson[256];
    if (doc["data"].is<JsonObject>()) {
      serializeJson(doc["data"], dataJson, sizeof(dataJson));
    } else {
      strcpy(dataJson, "{}");
    }
    if (name) sendAppEvent(name, dataJson);
    return;
  }

  // Anything else → user callback if registered.
  if (_onMessage) _onMessage(transportName, type, doc);
}
```

- [ ] **Step 5: Implement onCourierConnectionChange — internal status handler + user delegate**

```cpp
void Sandbox::onCourierConnectionChange(Courier::State state)
{
  using S = Courier::State;

  // Resident's internal status-text handling. Runs unconditionally if a
  // statusDisplay is configured. User's onConnectionChange callback runs
  // after, in addition (does not replace).
  switch (state) {
    case S::WifiConnecting:        showStatusText("WiFi..."); break;
    case S::WifiConfiguring:       showStatusText("Configure WiFi"); break;
    case S::WifiConnected:         showStatusText("WiFi connected"); break;
    case S::TransportsConnecting:  showStatusText("Connecting..."); break;
    case S::Connected:             showStatusText("Connected"); break;
    case S::Reconnecting:          showStatusText("Reconnecting..."); break;
    case S::ConnectionFailed:      showStatusText("Connection failed"); break;
    default: break;
  }

  if (_config.statusLED) {
    switch (state) {
      case S::WifiConnecting:
      case S::WifiConfiguring:       _config.statusLED->solidColor(0xFFFF00); break;
      case S::WifiConnected:
      case S::TransportsConnecting:  _config.statusLED->solidColor(0x00FFFF); break;
      case S::Connected:             _config.statusLED->solidColor(0x00FF00); break;
      case S::Reconnecting:          _config.statusLED->solidColor(0xFF8800); break;
      case S::ConnectionFailed:      _config.statusLED->solidColor(0xFF0000); break;
      default: break;
    }
  }

  if (_onConnectionChange) _onConnectionChange(state);
}

void Sandbox::onCourierConnected() {
  if (_onConnected) _onConnected();
}

void Sandbox::onCourierTransportsWillConnect() {
  // Resident's default: built-in WS gets /agents/<deviceType>-agent/<deviceId>.
  // User callback runs after and can override (e.g. set /devices/<id>).
  String wsPath = String("/agents/") + getDeviceType() + "-agent/" + _deviceId;
  ws().setEndpoint(_config.network->host ? _config.network->host : "localhost",
                   443, wsPath.c_str());
  Serial.printf("[resident] WS path: %s\n", wsPath.c_str());

  if (_onTransportsWillConnect) _onTransportsWillConnect();
}

void Sandbox::showStatusText(const char* text)
{
  if (!_config.statusDisplay) return;
  if (_lastStatusText == text) return;
  _lastStatusText = text;
  _config.statusDisplay->displayText(text);
}
```

- [ ] **Step 6: Extend Sandbox::loop() to drive Courier**

Find the existing `void Sandbox::loop()` method. Replace its body:

```cpp
void Sandbox::loop() {
  if (_courier.has_value()) {
    _courier->loop();
  }
  if (_config.statusDisplay) _config.statusDisplay->update();

  if (!_lua) return;

  // Standalone path always ticks; networked path gates on isConnected
  // (matches today's Device::loop behavior).
  bool shouldTick = !_courier.has_value() || isConnected();
  if (!shouldTick) return;

  // Existing extension update + tick logic — preserve as-is from current loop().
  for (uint8_t i = 0; i < _config.extensions.count; i++) {
    _config.extensions.items[i]->update();
  }
  if (!_appRunning) return;

  unsigned long now = millis();
  unsigned long elapsed = now - _lastTickTime;
  if (elapsed >= TICK_INTERVAL) {
    callOnTick(elapsed);
    _lastTickTime = now;
  }
  processNextEvent();
}
```

- [ ] **Step 7: Build the m5stick-demo example to confirm Sandbox still compiles**

Run: `cd examples/m5stick-demo/device && pio run`
Expected: PASS — Sandbox source compiles with the new orchestration. `Resident::Device` (still in the tree) continues to use its own setup/loop and is unaffected.

- [ ] **Step 8: Run unit tests**

Run: `./tools/run-tests.py unit`
Expected: PASS.

- [ ] **Step 9: Run static analysis**

Run: `./tools/run-tests.py static-analysis`
Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add src/ResidentSandbox.h src/ResidentSandbox.cpp
git commit -m "feat(sandbox): wire setup/loop orchestration with Courier integration

Sandbox::setup() now fires onConfigureNetwork, sets AP name, starts the
Lua sandbox, and kicks off Courier when networking is configured.
Internal handlers route reserved message types (app/shader/app_event) and
drive statusDisplay/statusLED on connection state transitions; user
callbacks fire in addition. Standalone mode (no cfg.network) skips all
Courier work.

Resident::Device is now a redundant wrapper but kept until examples migrate."
```

---

## Task 5: Update `Resident.h` umbrella

Drop the `ResidentDeviceConfig.h` include since `SandboxConfig` now carries everything. Keep `ResidentDevice.h` available for backward compatibility during the migration window — examples still include it directly.

**Files:**
- Modify: `src/Resident.h`

- [ ] **Step 1: Update Resident.h**

Replace `src/Resident.h` with:

```cpp
// src/Resident.h
#ifndef RESIDENT_H
#define RESIDENT_H

#include "ResidentSandbox.h"
#include "ResidentSandboxConfig.h"
#include "ResidentDriver.h"
#include "ResidentExtension.h"
#include "ResidentExtensions.h"
#include "ResidentLuaModule.h"
#include "ResidentStatusLED.h"
#include "ResidentStatusDisplay.h"

#endif // RESIDENT_H
```

(`ResidentDevice.h` is intentionally NOT in the umbrella — it'll be deleted after examples migrate.)

- [ ] **Step 2: Confirm m5stick-demo still builds (uses ResidentDevice.h directly)**

Run: `cd examples/m5stick-demo/device && pio run`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add src/Resident.h
git commit -m "refactor(headers): drop ResidentDeviceConfig from umbrella

SandboxConfig now carries deviceType/statusDisplay/statusLED/network and
supersedes DeviceConfig. ResidentDevice.h kept available for the migration
window; examples include it directly until they switch."
```

---

## Task 6: Migrate `examples/m5stick-demo/device/src/main.cpp`

Rewrite to use the new `Resident::Sandbox` API. Subclass goes away; lifecycle hooks become callback registrations in `setup()`.

**HARDWARE-VERIFICATION GATE:** Build clean locally, then **wait for Matt to flash and confirm** before committing. Do not commit if Matt has not verified.

**Files:**
- Modify: `examples/m5stick-demo/device/src/main.cpp`

- [ ] **Step 1: Replace main.cpp**

Replace `examples/m5stick-demo/device/src/main.cpp` with:

```cpp
#include <M5Unified.h>
#include <Resident.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"

// Default endpoint: the canonical Resident relay. Devs can self-host by
// changing RESIDENT_HOST below (or extending Courier with a config portal).
// The relay speaks the Resident canonical protocol:
//   wss://<host>/devices/<deviceId>            ← device WS (here)
//   POST https://<host>/devices/<deviceId>/send  ← skill/curl pushes JSON
static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

// Board-specific button pins. M5StickC Plus2 (ESP32 classic): GPIO 37 + 39.
// M5StickS3 (ESP32-S3 with OPI PSRAM): GPIO 11 + 12. On the S3, GPIO 37 is
// part of the OPI PSRAM interface — reading it via digitalRead() triggers a
// watchdog reset.
#if defined(BOARD_M5STICKS3)
static constexpr uint8_t BUTTON_PINS[] = {11, 12};
#else  // BOARD_M5STICK_C_PLUS2 (default)
static constexpr uint8_t BUTTON_PINS[] = {37, 39};
#endif
static constexpr PushButtonsConfig buttonConfig = {.numButtons = 2, .pins = BUTTON_PINS};

DisplayDriver displayDriver;
IMUDriver imuDriver;
BuzzerDriver buzzerDriver{255};
PushButtonsDriver buttonDriver{buttonConfig};

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "stick";
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver, &buttonDriver};
    cfg.statusDisplay = &displayDriver;
    cfg.network       = Courier::Config{
      .host = RESIDENT_HOST,
      .port = RESIDENT_PORT,
    };
    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for USB CDC on M5StickS3; harmless on M5Stick
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);

    // Override the default /agents/<type>-agent/<deviceId> path with the
    // canonical /devices/<deviceId> path used by resident.inanimate.tech.
    sandbox.onTransportsWillConnect([]() {
        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
    });

    // On first successful connection, replace the StatusDisplay's "Connected"
    // text with a sandbox app that shows the device ID prominently (so the
    // user knows what to push to). A real app sent via push-app or
    // send-app.sh will replace this.
    //
    // Function-local static guards against re-firing on reconnect — onConnected
    // fires every time we transition to Connected, but we only want the
    // bootstrap app to load once per boot (otherwise reconnection would clobber
    // whatever app the user pushed).
    sandbox.onConnected([]() {
        static bool loaded = false;
        if (loaded) return;
        loaded = true;
        String app = "function init(ctx)\n"
                     "  screen.clear()\n"
                     "  screen.text(10, 15, 'Resident', 3)\n"
                     "  screen.text(10, 60, 'Device ID:', 2)\n"
                     "  screen.text(10, 90, '";
        app += sandbox.getDeviceId();
        app += "', 3, 0, 255, 0)\n"
               "  screen.flip()\n"
               "end\n";
        sandbox.loadApp(app.c_str());
    });

    sandbox.setup();
}

void loop() {
    M5.update();
    sandbox.loop();
}
```

- [ ] **Step 2: Build clean locally**

Run: `cd examples/m5stick-demo/device && pio run`
Expected: PASS — compiles for both `m5stick` and `m5sticks3` envs.

- [ ] **Step 3: PAUSE for hardware verification**

**Do not commit.** Hand the build to Matt and wait for confirmation that:
- The board boots, joins WiFi via captive portal, connects to the relay.
- The TFT shows status text (WiFi → Connecting → Connected → device ID).
- A pushed app via `/resident:push-app` runs and exercises buttons + IMU + buzzer.

If Matt reports a regression, debug and re-build before re-presenting.

- [ ] **Step 4: Commit (only after Matt confirms)**

```bash
git add examples/m5stick-demo/device/src/main.cpp
git commit -m "refactor(m5stick-demo): migrate to Resident::Sandbox callback API

class DemoDevice subclass removed; setup() now registers
onTransportsWillConnect + onConnected callbacks against the global Sandbox
instance. Behavior identical to the old Device-based implementation;
verified on hardware (M5StickC Plus2 + M5StickS3)."
```

---

## Task 7: Migrate `examples/adafruit-esp32-s2-feather/device/src/main.cpp`

Rewrite to use the new `Resident::Sandbox` API.

**HARDWARE-VERIFICATION GATE:** Build clean locally, then **wait for Matt to flash and confirm** before committing.

**Files:**
- Modify: `examples/adafruit-esp32-s2-feather/device/src/main.cpp`

- [ ] **Step 1: Replace main.cpp**

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Resident.h>

#include "DisplayDriver.h"
#include "LEDDriver.h"
#include "BatteryDriver.h"

static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static Adafruit_LC709203F battery;
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
static bool batteryReady = false;

static DisplayDriver displayDriver{&tft, TFT_BACKLITE};
static LEDDriver ledDriver{&pixel};
static BatteryDriver batteryDriver{&battery, &batteryReady};

static Resident::SandboxConfig makeConfig() {
  Resident::SandboxConfig cfg;
  cfg.deviceType    = "feather-tft";
  // DisplayDriver dual-inherits as the StatusDisplay so connection-state
  // text gets drawn straight to the TFT before any app loads. LEDDriver
  // dual-inherits as the StatusLED so the NeoPixel reflects connection
  // state (yellow→cyan→green) until an app takes over.
  cfg.extensions    = {&displayDriver, &ledDriver, &batteryDriver};
  cfg.statusDisplay = &displayDriver;
  cfg.statusLED     = &ledDriver;
  cfg.network       = Courier::Config{
    .host = RESIDENT_HOST,
    .port = RESIDENT_PORT,
  };
  return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for USB-CDC enumeration on the host.

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 TFT Feather — Resident (full) ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // NeoPixel: data on PIN_NEOPIXEL, power on NEOPIXEL_POWER. Variant
  // declares NEOPIXEL_POWER_ON = HIGH for this rev.
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  pixel.begin();
  pixel.setBrightness(20);
  pixel.setPixelColor(0, 0x0000FF);  // blue = booting
  pixel.show();

  // TFT_I2C_POWER gates both the TFT and the I2C bus (one pin, two rails).
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // ST7789, 240x135 portrait native. Rotation 0 keeps it portrait
  // (135 wide × 240 tall) with USB-C at the bottom.
  tft.init(135, 240);
  tft.setRotation(0);

  Wire.begin();
  if (battery.begin()) {
    battery.setPackSize(LC709203F_APA_500MAH);
    batteryReady = true;
    Serial.println("LC709203 OK");
  } else {
    Serial.println("LC709203 not found — likely no battery plugged in");
  }

  // Override the default /agents/<type>-agent/<id> path with the canonical
  // /devices/<id> path used by resident.inanimate.tech.
  sandbox.onTransportsWillConnect([]() {
    String wsPath = String("/devices/") + sandbox.getDeviceId();
    sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
  });

  // On first successful connection, replace the StatusDisplay's text with
  // a tiny Lua app that paints the device ID on the TFT and sets the
  // NeoPixel green. A real app sent via push-app replaces this. Function-
  // local static guards against re-firing on reconnect (would otherwise
  // clobber a user-pushed app).
  sandbox.onConnected([]() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    String app = "function init(ctx)\n"
                 "  screen.clear()\n"
                 "  screen.text(5, 5, 'Resident', 2, 0, 255, 255)\n"
                 "  screen.text(5, 30, 'feather-tft', 1)\n"
                 "  screen.text(5, 55, 'Device ID:', 1, 200, 200, 200)\n"
                 "  screen.text(5, 75, '";
    app += sandbox.getDeviceId();
    app += "', 2, 0, 255, 0)\n"
           "  screen.flip()\n"
           "  led.set_brightness(20)\n"
           "  led.set(0, 255, 0)\n"
           "end\n";
    sandbox.loadApp(app.c_str());
  });

  // Hand off to Resident. It owns: WiFi (via WiFiManager captive portal
  // on first boot, persisted to NVS thereafter), time sync (via ezTime),
  // WebSocket connection (via Courier), Lua sandbox lifecycle, and
  // routing inbound `app`/`shader`/`app_event` messages to the sandbox.
  sandbox.setup();
}

void loop() {
  sandbox.loop();
}
```

- [ ] **Step 2: Build clean locally**

Run: `cd examples/adafruit-esp32-s2-feather/device && pio run`
Expected: PASS.

- [ ] **Step 3: PAUSE for hardware verification**

Wait for Matt to confirm: TFT shows status, NeoPixel reflects connection state, battery reads work, pushed app runs.

- [ ] **Step 4: Commit (only after Matt confirms)**

```bash
git add examples/adafruit-esp32-s2-feather/device/src/main.cpp
git commit -m "refactor(feather-tft): migrate to Resident::Sandbox callback API

class FeatherDevice subclass removed; setup() registers
onTransportsWillConnect + onConnected callbacks. Verified on hardware."
```

---

## Task 8: Migrate `examples/adafruit-esp32-s2-feather/device-minimal-resident/src/main.cpp`

Same migration pattern as Task 7, but for the minimal-Resident project (no hardware drivers; only a TFTStatusDisplay).

**HARDWARE-VERIFICATION GATE:** as above.

**Files:**
- Modify: `examples/adafruit-esp32-s2-feather/device-minimal-resident/src/main.cpp`

- [ ] **Step 1: Replace main.cpp**

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Resident.h>

// The canonical Resident relay. Devs can self-host by changing this; see the
// m5stick-demo example for the self-hosted Cloudflare Worker pattern.
static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static Adafruit_LC709203F battery;
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
static bool batteryReady = false;

// TFT-backed StatusDisplay. Resident calls displayText() with short status
// strings like "WiFi", "Connecting", "Connected" — and (once we open a WS)
// the device id, which is what the user needs to push apps. We draw it big.
class TFTStatusDisplay : public Resident::StatusDisplay {
public:
  void begin() override {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);
  }

  void displayText(const char* text) override {
    bool looksLikeId = (strlen(text) == 8);
    tft.fillScreen(ST77XX_BLACK);

    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(2);
    tft.setCursor(5, 5);
    tft.print("Resident");

    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 30);
    tft.print("ESP32-S2 TFT Feather");

    tft.setTextColor(looksLikeId ? ST77XX_GREEN : ST77XX_YELLOW);
    tft.setTextSize(looksLikeId ? 3 : 2);
    tft.setCursor(5, 75);
    tft.print(text);
  }
};

static TFTStatusDisplay tftStatus;

static Resident::SandboxConfig makeConfig() {
  Resident::SandboxConfig cfg;
  cfg.deviceType    = "feather-tft";
  cfg.statusDisplay = &tftStatus;
  cfg.network       = Courier::Config{
    .host = RESIDENT_HOST,
    .port = RESIDENT_PORT,
  };
  // No Lua hardware modules yet — apps get only the sandbox-generic surface
  // (log, time, kv, math, shader globals). Next steps: expose screen.* (TFT),
  // led.* (NeoPixel), battery.* (LC709203).
  return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 TFT Feather — Resident ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());

  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  pixel.begin();
  pixel.setBrightness(20);
  pixel.setPixelColor(0, 0x0000FF);
  pixel.show();

  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  tft.init(135, 240);
  tft.setRotation(3);

  Wire.begin();
  if (battery.begin()) {
    battery.setPackSize(LC709203F_APA_500MAH);
    batteryReady = true;
    Serial.println("LC709203 OK");
  } else {
    Serial.println("LC709203 not found — likely no battery plugged in");
  }

  // Override the default /agents/<type>-agent/<id> path with the canonical
  // /devices/<id> path used by resident.inanimate.tech.
  sandbox.onTransportsWillConnect([]() {
    String wsPath = String("/devices/") + sandbox.getDeviceId();
    sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
  });

  // On first successful connection, log the device id and redraw the TFT
  // with the device id prominently displayed (since the StatusDisplay path
  // will be suppressed once an app is running). Function-local static
  // guards against re-firing on reconnect.
  sandbox.onConnected([]() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    String app = "function init(ctx)\n"
                 "  log.info('feather-tft ready, id=" + sandbox.getDeviceId() + "')\n"
                 "end\n";
    sandbox.loadApp(app.c_str());
    tftStatus.displayText(sandbox.getDeviceId().c_str());
  });

  sandbox.setup();
}

void loop() {
  sandbox.loop();

  // Bring-up indicators stay alive alongside Resident's loop:
  //   - red LED toggles at 2 Hz (the firmware is running)
  //   - NeoPixel reflects connection state (green=connected, yellow=not)
  static uint32_t lastBlink = 0;
  uint32_t now = millis();
  if (now - lastBlink >= 500) {
    lastBlink = now;
    static bool ledOn = false;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn);
    pixel.setPixelColor(0, sandbox.isConnected() ? 0x00FF00 : 0xFFFF00);
    pixel.show();
  }
}
```

- [ ] **Step 2: Build clean locally**

Run: `cd examples/adafruit-esp32-s2-feather/device-minimal-resident && pio run`
Expected: PASS.

- [ ] **Step 3: PAUSE for hardware verification**

Wait for Matt to flash and confirm WiFi + status text + log-only Lua app behavior.

- [ ] **Step 4: Commit (only after Matt confirms)**

```bash
git add examples/adafruit-esp32-s2-feather/device-minimal-resident/src/main.cpp
git commit -m "refactor(feather-minimal): migrate to Resident::Sandbox callback API

Verified on hardware."
```

---

## Task 9: Migrate `examples/espidf-basic/main/main.cpp`

ESP-IDF entrypoint pattern — `app_main()` instead of Arduino's `setup()`/`loop()`. Same callback-registration pattern.

**HARDWARE-VERIFICATION GATE:** This example targets `example.com` and won't actually connect. The "build clean" + ESP-IDF CI run is the verification. Hardware not required.

**Files:**
- Modify: `examples/espidf-basic/main/main.cpp`

- [ ] **Step 1: Replace main.cpp**

```cpp
// Minimal ESP-IDF example for Resident. Demonstrates:
//   - Constructing a Resident::Sandbox at file scope
//   - Registering a driver declaratively via SandboxConfig::extensions
//   - Running setup()/loop() from app_main() rather than autostarted Arduino
//
// This intentionally targets `example.com` — it won't actually connect.
// Real consumers point `host` at their own Resident server.

#include <Arduino.h>
#include <Resident.h>
#include "StubLEDDriver.h"

static StubLEDDriver led;

static Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType = "espidf-basic";
    cfg.extensions = {&led};
    cfg.network    = Courier::Config{
      .host = "example.com",
    };
    return cfg;
}

static Resident::Sandbox sandbox{makeConfig()};

extern "C" void app_main() {
    initArduino();
    sandbox.setup();
    while (true) {
        sandbox.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

- [ ] **Step 2: Build via ESP-IDF locally (or rely on CI)**

Run (if ESP-IDF is installed locally):
```sh
cd examples/espidf-basic
./tools/fetch-deps.sh
idf.py build
```

Expected: PASS.

If ESP-IDF isn't installed, skip the local build — the `build-espidf` CI job will exercise it after push.

- [ ] **Step 3: Commit**

```bash
git add examples/espidf-basic/main/main.cpp
git commit -m "refactor(espidf-basic): migrate to Resident::Sandbox callback API"
```

---

## Task 10: Delete `Resident::Device`

All examples are now on the new API. Remove the dead code.

**Files:**
- Delete: `src/ResidentDevice.h`
- Delete: `src/ResidentDevice.cpp`
- Delete: `src/ResidentDeviceConfig.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Verify no remaining references**

Run:
```sh
git grep -nE 'Resident::Device\b|ResidentDevice\.h|DeviceConfig\b' \
  -- ':!docs/changelog.md' \
     ':!docs/superpowers/specs/' \
     ':!examples/*/docs/'
```

Expected: zero output. If any references remain, fix them before proceeding.

- [ ] **Step 2: Delete the files**

```sh
git rm src/ResidentDevice.h src/ResidentDevice.cpp src/ResidentDeviceConfig.h
```

- [ ] **Step 3: Update CMakeLists.txt**

Edit `CMakeLists.txt`. Find the `idf_component_register` call and remove `src/ResidentDevice.cpp` from `SRCS`:

```cmake
idf_component_register(
    SRCS "src/ResidentSandbox.cpp"
    INCLUDE_DIRS "src"
    REQUIRES ${RESIDENT_REQUIRES}
)
```

- [ ] **Step 4: Run unit tests**

Run: `./tools/run-tests.py unit`
Expected: PASS.

- [ ] **Step 5: Run static analysis**

Run: `./tools/run-tests.py static-analysis`
Expected: PASS.

- [ ] **Step 6: Build all examples to confirm nothing references Device**

```sh
cd examples/m5stick-demo/device && pio run
cd ../../adafruit-esp32-s2-feather/device && pio run
cd ../device-minimal-resident && pio run
```

All Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: delete Resident::Device — superseded by Resident::Sandbox

All examples migrated; the Device class is no longer referenced. Removes
src/ResidentDevice.{h,cpp}, src/ResidentDeviceConfig.h, and the
ResidentDevice.cpp entry in idf_component_register."
```

---

## Task 11: Expand CI build matrix to all PlatformIO examples

`tools/run-tests.py build` currently builds only `m5stick-demo`. Iterate over a list so the Feather projects get built too. Add the bring-up project (`device-no-resident`) as a smoke test.

**Files:**
- Modify: `tools/run-tests.py`

- [ ] **Step 1: Read the current build command**

Run: `sed -n '69,80p' tools/run-tests.py`

Confirm it currently has a single hard-coded `project = ROOT / "examples" / "m5stick-demo" / "device"`.

- [ ] **Step 2: Refactor to iterate**

Find the `build_verification` function and replace its body:

```python
@cli.command("build")
def build_verification() -> None:
    """Build-verify every PlatformIO example."""
    click.echo(click.style("Build Verification", fg="white", bold=True))
    projects = [
        ROOT / "examples" / "m5stick-demo" / "device",
        ROOT / "examples" / "adafruit-esp32-s2-feather" / "device",
        ROOT / "examples" / "adafruit-esp32-s2-feather" / "device-minimal-resident",
        ROOT / "examples" / "adafruit-esp32-s2-feather" / "device-no-resident",
    ]
    failed: list[Path] = []
    for project in projects:
        ok = run_cmd(["pio", "run"], cwd=project,
                     label=f"pio run ({project.parent.name}/{project.name})")
        if not ok:
            failed.append(project)
    if failed:
        click.echo(click.style(
            f"✗ Build failed for: {', '.join(str(p.relative_to(ROOT)) for p in failed)}",
            fg="red"))
        sys.exit(1)
    click.echo(click.style("✓ All builds passed", fg="green"))
```

- [ ] **Step 3: Run locally**

Run: `./tools/run-tests.py build`
Expected: PASS for all four projects. If any fails, debug — the rename should have left them all building.

(This run will take longer than before — Feather projects fetch Adafruit libs from the registry on first build.)

- [ ] **Step 4: Commit**

```bash
git add tools/run-tests.py
git commit -m "ci: build all PlatformIO examples (m5stick-demo + 3 feather projects)

The previous build target only exercised m5stick-demo. After the
Resident::Sandbox rename, all Resident-using example projects need CI
coverage so future API changes can't slip through unbuilt. Includes
device-no-resident as a smoke test for the bring-up baseline."
```

---

## Task 12: Update README, start-building, api docs

The user-facing core docs. Replace every reference to `Resident::Device` with `Resident::Sandbox`; collapse the two-example structure (standalone vs. connected) into one example with a sidebar; rewrite step 2 of the bring-up guide.

**Files:**
- Modify: `README.md`
- Modify: `docs/start-building.md`
- Modify: `docs/api.md`

- [ ] **Step 1: Rewrite README.md sections**

The README currently has two sections "With `Resident::Device` (network-connected)" and "Standalone `Resident::Sandbox` (no network)". Collapse into one example using `Resident::Sandbox`. Use the m5stick-demo or feather-tft golden path code from this plan's Task 6/7 as the canonical example. Add a note: *"Omit `cfg.network` to run the sandbox without WiFi."*

Verify the example builds match the actual file contents (e.g., the `RESIDENT_HOST` constant matches what the example uses).

- [ ] **Step 2: Rewrite docs/start-building.md step 2**

Step 2 is "Add Resident". Replace the current `Resident::Device` subclass instructions with the new `Resident::Sandbox` callback-registration pattern. Specifically:

- The `Resident::DeviceConfig` snippet becomes `Resident::SandboxConfig` with `cfg.network = Courier::Config{...}`.
- The "Subclass `Resident::Device`, override `onTransportsWillConnect()`" instruction becomes "Register `sandbox.onTransportsWillConnect([]() { ... })` from `setup()`."
- Reference the actual file paths of the migrated minimal-resident example.

Cross-check that the prose still tracks the file structure under `examples/adafruit-esp32-s2-feather/`.

- [ ] **Step 3: Rewrite docs/api.md**

Full sweep. Drop all `Resident::Device` content. Document:
- `Resident::SandboxConfig` field shape
- `Resident::Sandbox` constructor, `setup()`, `loop()`
- All `on*` callback registration methods (signature + when each fires)
- `loadApp`, `loadShader`, `sendAppEvent`, `setTimezone`, `hasTimezone`
- Accessors: `getDeviceId`, `getDeviceType`, `isConnected`, `isTimeSynced`, `courier()`, `ws()`, `hasNetwork()`
- `Resident::Driver` / `Resident::Extension` lifecycle (unchanged from today)
- `Resident::StatusDisplay`, `Resident::StatusLED` interfaces
- The lifecycle-ordering diagram from the spec

- [ ] **Step 4: Verify links**

Run: `grep -nE 'ResidentDevice|Resident::Device' README.md docs/start-building.md docs/api.md`
Expected: zero hits.

- [ ] **Step 5: Commit**

```bash
git add README.md docs/start-building.md docs/api.md
git commit -m "docs: rewrite core docs around Resident::Sandbox callback API

README collapses standalone/connected examples into one configurable
example. start-building step 2 documents the callback pattern. api.md
fully replaces Resident::Device content with the new Sandbox surface."
```

---

## Task 13: Update example READMEs + write-device-skill SKILL.md

The peripheral docs — example READMEs that reference the C++ surface, plus the agent-skill walkthrough that may reference Device.

**Files:**
- Modify: `examples/adafruit-esp32-s2-feather/device/README.md`
- Modify: `examples/adafruit-esp32-s2-feather/device-minimal-resident/README.md`
- Modify: `tools/agent-plugin/skills/write-device-skill/SKILL.md`

- [ ] **Step 1: Read each file and identify Device references**

Run:
```sh
grep -nE 'Resident::Device|ResidentDevice|DeviceConfig' \
  examples/adafruit-esp32-s2-feather/device/README.md \
  examples/adafruit-esp32-s2-feather/device-minimal-resident/README.md \
  tools/agent-plugin/skills/write-device-skill/SKILL.md
```

For each hit, decide whether it's:
- A **code sample** (rewrite to use Resident::Sandbox)
- A **prose reference** (rewrite the sentence)
- An **historical note** (probably leave; flag if uncertain)

- [ ] **Step 2: Rewrite each occurrence in place**

Use Edit operations to fix each hit. Ensure the new code samples are consistent with the migrated examples (Tasks 7 + 8).

For `write-device-skill/SKILL.md` specifically: this skill walks an agent through writing a DEVICE-SKILL.md for a new board. If it has a "Here's how a Resident-based device's main.cpp looks" section, update that snippet to the Sandbox pattern.

- [ ] **Step 3: Verify**

Run the same grep again — expected: zero hits, modulo any deliberate historical references.

- [ ] **Step 4: Commit**

```bash
git add examples/adafruit-esp32-s2-feather/device/README.md \
        examples/adafruit-esp32-s2-feather/device-minimal-resident/README.md \
        tools/agent-plugin/skills/write-device-skill/SKILL.md
git commit -m "docs(examples,skills): replace Resident::Device references with Sandbox"
```

---

## Task 14: Write `docs/migration-0.4-to-0.5.md`

A targeted guide for agents (or humans) migrating an existing 0.4.x firmware project to 0.5.0. Modeled on Courier's `docs/migration-0.3-to-0.4.md` — sectioned by topic, old-vs-new tables for renames, before/after code blocks for behavior changes. The four migrated examples in this PR are the source material — every pattern in the guide is something we just translated for real.

The audience is someone with an existing 0.4.x `main.cpp` who needs to translate it. Not a tutorial, not a spec — a translation reference.

**Files:**
- Create: `docs/migration-0.4-to-0.5.md`

- [ ] **Step 1: Write the migration guide**

Create `docs/migration-0.4-to-0.5.md`:

````markdown
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
cfg.network       = Courier::Config{
  .host = "resident.inanimate.tech",
  // .dns1 = ..., .port = ..., etc. — anything Courier::Config takes
};
```

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
  cfg.network       = Courier::Config{ .host = "resident.inanimate.tech" };
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

Net difference: ~5 lines shorter, the subclass is gone, and the bootstrap-app pattern is now expressed against the right lifecycle hook (`onConnected`) rather than polled in a `deviceLoop()`.

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
4. Replace `Resident::DeviceConfig` with `Resident::SandboxConfig`; move `host`/`port`/`dns1`/`dns2` into `cfg.network = Courier::Config{...}`.
5. In `setup()`/`loop()`, replace `device.setup()`/`device.loop()` with `sandbox.setup()`/`sandbox.loop()`.
6. Inside any `onConnected` / bootstrap-app callback, add a `static bool loaded = false; if (loaded) return; loaded = true;` guard if you want one-shot semantics.
7. Inside any old `onMessage` override: delete the `Resident::Device::onMessage(...)` super call, and delete any `if (type == "app" / "shader" / "app_event")` branches — Resident handles those internally now.
8. Any reach into `_ws` becomes `sandbox.ws()`. Any reach into `courier()` keeps its name.
9. Build. Cppcheck/compile errors will surface anything missed; signature mismatches in `onMessage` are the most common.
````

- [ ] **Step 2: Cross-check against migrated examples**

Open the migrated examples (Tasks 6, 7, 8, 9) side-by-side with the guide. For each `Resident::Device` pattern in the historical pre-migration code, confirm the guide shows that pattern's translation. Any pattern present in an example but missing from the guide is a guide gap; add it.

Concretely, walk through the four migrated `main.cpp` files and check that every line shape (config build, subclass override, lifecycle hook, accessor call) is represented somewhere in the guide.

- [ ] **Step 3: Verify the guide compiles in the head**

Read the guide top to bottom as if you'd never seen the new API before. The only context you should need is: "I have an existing `main.cpp` using `Resident::Device`; translate it." If a section presupposes knowledge from earlier sections, reorder or cross-reference. If a code sample uses a function or type that hasn't been introduced yet, add a one-line aside explaining it.

- [ ] **Step 4: Commit**

```bash
git add docs/migration-0.4-to-0.5.md
git commit -m "docs: add migration guide from Resident 0.4 to 0.5

Targeted at agents (or humans) migrating existing 0.4 firmware to the new
Resident::Sandbox + callback API. Modeled on courier/docs/migration-0.3-to-0.4.md.
Every translation pattern in the guide is something we just translated for
real in the four example projects in this PR."
```

---

## Task 15: Update changelog + bump version to 0.5.0

Record the breaking-changes story. Bump library.json.

**Files:**
- Modify: `docs/changelog.md`
- Modify: `library.json`

- [ ] **Step 1: Add 0.5.0 entry to docs/changelog.md**

Insert at the top, above the `## v0.4.1-dev` entry:

```markdown
## v0.5.0

Theme: collapse `Resident::Device` into `Resident::Sandbox`; replace virtual
subclass hooks with callback registration to align with Courier's idiom.

### Breaking changes

**`Resident::Device` is gone.** The C++ surface is now a single
`Resident::Sandbox` class. Examples that previously subclassed Device need
to migrate to callback registration:

```cpp
// Before
class FeatherDevice : public Resident::Device {
  void onConnected() override { ... }
};
FeatherDevice device;
device.setup(); device.loop();

// After
Resident::Sandbox sandbox{cfg};
sandbox.onConnected([]() { ... });
sandbox.setup(); sandbox.loop();
```

**`Resident::DeviceConfig` is gone.** Its fields (`deviceType`,
`statusDisplay`, `statusLED`, `host`/`port`/`dns1`/`dns2`) are now on
`Resident::SandboxConfig`. Network-related fields move into
`cfg.network`, which is a `std::optional<Courier::Config>` — set it to
enable WiFi/transports; omit for standalone-runtime mode:

```cpp
Resident::SandboxConfig cfg;
cfg.deviceType    = "feather-tft";
cfg.extensions    = {&display, &led, &battery};
cfg.statusDisplay = &display;
cfg.statusLED     = &led;
cfg.network       = Courier::Config{ .host = "resident.inanimate.tech" };
```

**Header changes:**
- `<ResidentDevice.h>` → `<Resident.h>` (umbrella) or `<ResidentSandbox.h>`.
- `<ResidentDeviceConfig.h>` removed; use `<ResidentSandboxConfig.h>`
  (or `<Resident.h>`).

**`onMessage` semantics changed.** Resident now routes the reserved types
(`app`, `shader`, `app_event`) internally. Your `sandbox.onMessage(cb)`
callback only fires for unknown message types — the previous super-call
pattern (`Device::onMessage(...)` to keep sandbox routing) is no longer
needed and will not compile.

**Network customisation** (TLS cert, custom headers, additional transports,
custom WiFi configuration) is now done via `sandbox.onConfigureNetwork(cb)`,
which receives `Courier::Client&` directly. The previous pass-through
fields on `DeviceConfig` (`dns1`/`dns2`) move into `cfg.network` since
that field IS a `Courier::Config`. New Courier features become available
without a Resident change.

### Why

The old surface had three problems:
1. Story/code mismatch — landing page said "Sandbox", main.cpp said "Device".
2. `Resident::Device` overclaimed scope — it's a component on the board, not the board.
3. Different extension idiom from Courier (virtuals vs. callbacks) forced
   developers to switch mental models when crossing the boundary.

The single-class + callback model fixes all three.

### Migration

**See [`docs/migration-0.4-to-0.5.md`](migration-0.4-to-0.5.md) for the full
translation reference** — sectioned by topic, with old-vs-new tables and
before/after code blocks for every pattern. Mechanical in most cases.

The four example-project commits in this release are also worked references:
- `examples/m5stick-demo/device/src/main.cpp`
- `examples/adafruit-esp32-s2-feather/device/src/main.cpp`
- `examples/adafruit-esp32-s2-feather/device-minimal-resident/src/main.cpp`
- `examples/espidf-basic/main/main.cpp`
```

- [ ] **Step 2: Bump library.json**

Edit `library.json`:

```json
{
  "name": "resident",
  "version": "0.5.0",
  ...
}
```

- [ ] **Step 3: Commit**

```bash
git add docs/changelog.md library.json
git commit -m "chore(release): v0.5.0 — Resident::Sandbox rename + callback API"
```

---

## Task 16: Final verification sweep

Run every grep, every test, every build. Catch anything that snuck through.

- [ ] **Step 1: Grep for residual references**

```sh
git grep -nE 'Resident::Device\b|ResidentDevice\.h|ResidentDeviceConfig\.h|DeviceConfig\b' \
  -- ':!docs/changelog.md' \
     ':!docs/superpowers/specs/' \
     ':!docs/superpowers/plans/' \
     ':!examples/*/docs/'
```

Expected: zero hits. If any appear, decide whether to fix or whitelist (typically only historical-record paths get whitelisted).

- [ ] **Step 2: Grep for residual subclass-pattern indicators**

```sh
git grep -n 'public Resident::Device' -- 'examples/'
git grep -n 'deviceSetup\|deviceLoop' -- ':!docs/'
```

Expected: zero hits in either.

- [ ] **Step 3: Run the full test suite**

```sh
./tools/run-tests.py static-analysis
./tools/run-tests.py unit
./tools/run-tests.py build
```

All Expected: PASS.

- [ ] **Step 4: Confirm CI workflow file is current**

Run: `cat .github/workflows/ci.yml`

Confirm the `build-platformio` job calls `./tools/run-tests.py build` (which now iterates all projects). No CI-config change is needed because the iteration is in the python tool — but verify nothing requires updating.

- [ ] **Step 5: Commit any final cleanup**

If the previous steps surfaced anything to fix:

```bash
git add -A
git commit -m "chore: final sweep after Resident::Sandbox rename

<list anything found>"
```

If nothing needed cleanup, skip this step.

- [ ] **Step 6: Push and let CI verify**

```bash
git push -u origin sandbox-rename
```

Then watch the CI run. Expected: all four jobs pass (static-analysis, unit-tests, build-platformio, build-espidf).

---

## Done state

- `Resident::Sandbox` is the only public C++ class for runtime work.
- All four Resident-using example projects compile and (the three with hardware) verified by Matt.
- CI builds all four PlatformIO projects + the ESP-IDF project.
- Docs (README, start-building, api, changelog, example READMEs, write-device-skill skill) all reference the new API.
- Version bumped to 0.5.0.
- `git grep 'Resident::Device'` returns only historical references.
