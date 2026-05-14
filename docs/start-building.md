# Start building

**This guide is written for coding agents** to follow when adding a new ESP32 device to Resident. It assumes a working PlatformIO toolchain and the ability to flash firmware over USB. The Adafruit ESP32-S2 TFT Feather under [`examples/adafruit-esp32-s2-feather/`](../examples/adafruit-esp32-s2-feather/) is the worked example throughout; adapt the specifics to your hardware.

There are four steps:

1. **Bring up your hardware** — prove the board boots and each peripheral works. *No Resident yet.*
2. **Add Resident** — layer the Lua sandbox + Wi-Fi + WebSocket transport on top. Apps can run, but they only see built-in modules (`log`, `time`, `kv`, math).
3. **Add Resident drivers** — expose your hardware as Lua modules (`screen.*`, `led.*`, `battery.*`, …).
4. **Create and push apps** — use the agent skills to author a `DEVICE-SKILL.md`, generate Lua against it, and push to the default backend.

Each step finishes with something flashable. If a later step regresses, drop back to an earlier project as a known-good baseline.

The Feather example preserves three buildable PlatformIO projects matching steps 1, 2 baseline, and 2+3 combined:

```
examples/adafruit-esp32-s2-feather/
├── device-no-resident/        ← end of step 1
├── device-minimal-resident/   ← end of step 2 (Resident, no hardware modules)
└── device/                    ← end of step 3 (full drivers)
```

---

## Step 1 — Bring up your hardware

**Goal:** the smallest PlatformIO project that boots, prints chip identity over USB serial, and exercises each peripheral. No networking, no Lua, no Resident.

### What the agent should produce

A `src/main.cpp` that, in `setup()`, prints chip stats (model, cores, MHz, flash, PSRAM) and initialises each peripheral the board ships with. In `loop()`, something visible at a known cadence — a heartbeat blink, a NeoPixel cycle, a redrawn screen region — so success or failure is readable at a glance.

For the Feather: chip banner, I2C bus scan, LC709203 fuel gauge init, TFT init drawing chip info + a live "uptime / battery" block, NeoPixel cycling, status splash at the end of `setup()` (green = full success, yellow = soft warn like "no battery"). See [`device-no-resident/src/main.cpp`](../examples/adafruit-esp32-s2-feather/device-no-resident/src/main.cpp).

### Decisions to make

- **PlatformIO board ID.** Many vendors ship visually-similar boards under the same family name with different pin maps. Pick the *exact* board ID and verify by reading `~/.platformio/packages/framework-arduinoespressif32/variants/<board>/pins_arduino.h` — that file is the source of truth for `LED_BUILTIN`, `PIN_NEOPIXEL`, `SDA`/`SCL`, and any board-specific power-gate pins.
- **Serial routing.** On native-USB boards (ESP32-S2/S3 without a USB-to-serial bridge) `Serial` only reaches the USB-C jack if USB-CDC-on-boot is enabled. On the Feather that means `-DARDUINO_USB_CDC_ON_BOOT=1` in `build_flags`. Without it the chip seems dead.
- **Upload reset method.** Boards with a CP210x-style bridge auto-reset on DTR/RTS. Native-USB boards need esptool to issue a 1200-baud touch to the running firmware. Add `board_upload.use_1200bps_touch = true` and `board_upload.wait_for_upload_port = true` if the variant supports it. (The very first flash after a non-Arduino firmware still needs the manual BOOT + RESET button dance.)
- **Library Dependency Finder mode.** Some Adafruit sensor libs `#include` `SPI.h` from headers that PlatformIO's default `chain` LDF can't resolve. If you see fatal-error: `Adafruit_BusIO_*` not found, set `lib_ldf_mode = deep`.
- **A "ready" signal.** Decide how the board says "everything booted" without serial. If there's a display, draw chip info on it. If not, end `setup()` with a 1–2 s solid status colour on a NeoPixel (green for full pass, yellow for soft warning, red reserved for hard failures), then resume the running pattern.
- **Quirks worth documenting.** Note in the example's README anything that looks like a firmware bug but isn't (an LED that flashes from hardware not your code, a sensor that returns garbage until warmed up, a power-gate pin that has to settle for ms before peripherals respond). Future readers shouldn't go hunting.

### Why this matters before Resident

Resident pulls in WiFi, ezTime, WiFiManager, Courier, ArduinoJson, Esp32Lua, and the in-tree Resident library — a lot of code surface, any of which can fail in ways that look like "the board doesn't work". If the bare bring-up sketch already runs cleanly, the bug in the next step has to be in Resident's layer.

---

## Step 2 — Add Resident

**Goal:** the same hardware, now with Wi-Fi + Lua sandbox + WebSocket transport to the default relay. The device displays its 8-character device ID once connected and accepts apps over `push-app`. Apps can only call built-in modules (`log`, `time`, `kv`, math globals); hardware bindings come in step 3.

### What the agent should produce

The bring-up `setup()` code stays — peripheral init still happens before Resident takes over. On top:

- Add the in-tree Resident library to `lib_deps` (`symlink://<path-to-resident-root>`) along with its runtime deps: Courier (git URL), `tzapu/WiFiManager`, `bblanchon/ArduinoJson`, `ropg/ezTime`, `fischer-simon/Esp32Lua`.
- Add a custom `partitions.csv` — Resident's stack pushes the binary toward 1.2 MB; the default 4 MB layout's `app0` slice (~1.4 MB) leaves no head-room. The Feather example uses 2.5 MB app + 1.4 MB SPIFFS.
- Implement a `Resident::StatusDisplay` subclass that paints connection-state text (`"WiFi"`, `"Connecting"`, `"Connected"`, then the device ID) on whatever output the board has — a TFT, an OLED, a NeoPixel, or just plain serial. Resident's internal handler calls `displayText()` on every connection state transition automatically, no wiring required.
- Build a `Resident::SandboxConfig`: set `deviceType`, point `statusDisplay` at the subclass, leave `extensions` empty for this step. Assign `cfg.network` from a `Courier::Config` populated via direct field assignment (`courier.host = "resident.inanimate.tech"; cfg.network = courier;` — designated initializers don't compile under strict ESP-IDF builds).
- Construct a global `Resident::Sandbox sandbox{makeConfig()}`.
- Register `sandbox.onTransportsWillConnect([]() { sandbox.ws().setEndpoint(host, 443, "/devices/" + sandbox.getDeviceId()); })` to override the default `/agents/<type>-agent/<id>` path with the canonical `/devices/<id>` path used by `resident.inanimate.tech`.
- `setup()` calls `sandbox.setup()` after the hardware init block; `loop()` calls `sandbox.loop()`.

See [`device-minimal-resident/src/main.cpp`](../examples/adafruit-esp32-s2-feather/device-minimal-resident/src/main.cpp) for the Feather's version (~150 lines).

### Decisions to make

- **Relay endpoint.** The default public relay (`resident.inanimate.tech`) is the simplest path; `push-app` defaults to it. To self-host, deploy the Cloudflare Worker under `examples/m5stick-demo/server/` and replace the host constant. The protocol is identical.
- **`deviceType` string.** Logged on the relay side and used to seed the captive-portal AP name (`Resident <DeviceType> <ID-short>`). Keep it short and meaningful.
- **Wi-Fi UX.** Courier defaults to a WiFiManager captive portal: first boot exposes an open AP, credentials persist in NVS thereafter. For headless deployments, hardcode `WiFi.begin(ssid, pass)` instead — but the captive portal is friendlier for examples.
- **Pre-connection display.** What the board shows during `"WiFi"` / `"Connecting"` states is up to you. Whatever you pick, leave room for the 8-character device ID — that's the string the user needs to read off the board to push apps.
- **Bootstrap "I'm connected" app.** Once a WS connects, you can either draw the device ID directly from C++ or have the firmware auto-load a tiny built-in Lua app that does the drawing. The Lua-app pattern means the user's first impression of a Resident device is *a Lua app running*, which is the right mental model — and a real app pushed via `push-app` replaces it cleanly.

### What the agent should be able to demonstrate at this point

1. Flash; open serial monitor.
2. On first boot, join the captive-portal AP and enter Wi-Fi credentials.
3. Watch the display cycle `WiFi` → `Connecting` → `Connected` → device ID.
4. `/resident:push-app --device-id <id> some-app.lua` with an app that only calls `log.info(...)` — confirm the message reaches the device and runs. (Apps using `screen.*` etc. will hit a Lua runtime error here — that's expected; step 3 fixes it.)

---

## Step 3 — Add Resident drivers

**Goal:** expose each hardware peripheral as a Lua module so apps can drive it. Each peripheral becomes its own `Resident::Driver` subclass; drivers live in a project-local `lib/drivers/` directory and are registered as extensions on the `SandboxConfig`.

### Pattern

A driver is a class that inherits `Resident::Driver` and implements:

- `name()` returning the Lua module name (e.g. `"screen"`, `"led"`, `"imu"`).
- `registerModule(LuaModule&)` chaining `.method<Class, &Class::cMethod>("lua_name")` calls. Each C++ method takes `lua_State*` and returns the number of Lua return values, pushing them with `lua_pushnumber` / `lua_pushinteger` / `lua_pushboolean`.
- Optionally: `begin()` for one-shot hardware init that happens inside `sandbox.setup()`; `onAppReset()` to clear per-app state (canvas, LED off) when a new app loads; `onAppRunning(bool)` to track sandbox state.

A driver can dual-inherit `Resident::StatusDisplay` or `Resident::StatusLED` — that's how the Feather's `DisplayDriver` and `LEDDriver` both serve as boot-time status indicators *and* as Lua-accessible modules. The handoff is automatic: once a Lua app loads, the driver gates its `displayText` / `solidColor` calls on `_appRunning` so Lua owns the hardware.

For each driver in `device/lib/drivers/`:

- One header + one cpp per driver.
- A single `library.json` at `lib/drivers/library.json` so PlatformIO picks them up as a project-local library.
- Add `symlink://lib/drivers` to `lib_deps` explicitly — once a parent `symlink://` is in the list (for Resident itself), PlatformIO's LDF stops auto-scanning the project's own `lib/`.

### Feather case study

The Feather exposes three drivers — see [`device/lib/drivers/src/`](../examples/adafruit-esp32-s2-feather/device/lib/drivers/src/):

- **`DisplayDriver`** wraps `Adafruit_ST7789`. Allocates a `GFXcanvas16` framebuffer in `begin()`, exposes `screen.{clear, text, fill_rect, rect, line, triangle, fill_triangle, pixel, flip, set_brightness, width, height}`. `flip()` pushes the canvas to the TFT in a single SPI transfer via `drawRGBBitmap` — double-buffering is essential for anything resembling a game loop, drawing primitives straight to the TFT one-at-a-time is too slow. Backlight brightness is on a LEDC PWM channel. Dual-inherits `Resident::StatusDisplay`.
- **`LEDDriver`** wraps `Adafruit_NeoPixel`. Exposes `led.{set, set_brightness, off}`. Dual-inherits `Resident::StatusLED` — Resident drives the pixel yellow/cyan/green/orange/red through connection states until an app loads, then `onAppRunning(true)` flips a flag and `solidColor()` becomes a no-op so the app fully owns the pixel.
- **`BatteryDriver`** wraps `Adafruit_LC709203F`. Exposes `battery.{voltage, percent, present}`. Returns zeros when no LiPo is connected (the chip is powered by VBAT, invisible on I2C without it).

### Decisions to make

- **One driver per hardware unit.** Don't merge `screen` and `led` just because they share `main.cpp`. Lua module names should reflect the device surface, not the C++ class hierarchy.
- **Where to allocate framebuffers.** Large back-buffers (a 16-bit canvas at typical small-display resolutions is tens of KB) can live in SRAM if it fits alongside Wi-Fi + Lua state, or be pushed to PSRAM if the chip has it. Measure before deciding; the Lua state alone is a few tens of KB once an app is loaded.
- **Lifecycle of driver state across apps.** On `onAppReset()`, restore a safe baseline: clear the canvas, turn the NeoPixel off, stop any audio. The next app should see fresh hardware. The example: `LEDDriver::onAppReset()` does `setPixelColor(0, 0); show();`.
- **Order of hardware init vs `sandbox.setup()`.** Build and `init()` the underlying Adafruit / vendor objects in `main.cpp` *before* calling `sandbox.setup()`. The driver's own `begin()` runs inside Resident's lifecycle and assumes the hardware is already up.
- **Event-emitting drivers.** Drivers can call `sendEvent(name, fields, count)` to surface hardware events to Lua's `on_event`. The m5stick-demo's `PushButtonsDriver` is the canonical example (button presses → `event.name == "button"`, `event.index == 0|1`).

### What the agent should be able to demonstrate

`/resident:push-app --device-id <id> some-app.lua` with an app that calls `screen.text(...)`, `led.set(...)`, `battery.voltage()`. The TFT updates, the NeoPixel changes colour, battery reads come back. The previous error (`attempt to index a nil value (global 'screen')`) is gone.

---

## Step 4 — Create and push apps

**Goal:** stop hand-writing Lua. Document the device's Lua surface in a `DEVICE-SKILL.md`, then use the Resident agent skills to generate apps against that surface, validate them locally, and push to the device.

### Author a `DEVICE-SKILL.md`

Place at the device's project root (e.g. [`examples/adafruit-esp32-s2-feather/device/DEVICE-SKILL.md`](../examples/adafruit-esp32-s2-feather/device/DEVICE-SKILL.md)). It documents:

- **Hardware** — what's on the board, in app-author terms (screen dimensions, NeoPixel count, sensor ranges). Not C++ internals.
- **Lua Modules** — one section per driver. Show the exact Lua calls with realistic argument shapes (`screen.text(x, y, str, size, r, g, b)`, defaults noted). Code-fenced ```lua blocks so the create-app skill can grep them.
- **Examples** — three to six short apps that exercise the modules. Hello-world, an animation, a sensor-driven demo, a button-handler if applicable.
- **Constraints** — screen resolution, colour ranges, memory considerations.
- **Practical Tips** — board-specific gotchas an app author needs to remember (e.g. "always call `screen.flip()` after a draw sequence", "clamp NeoPixel brightness in `init()`").
- **Validation stubs** — an optional ```lua block under `## Validation stubs` providing concrete return values for getter-style functions so the local validator doesn't return `nil` and crash apps that do arithmetic on getter results.
- **App mode / Shader mode** — note whether shader expressions are supported on the device.

The `/resident:write-device-skill` skill walks an agent through producing this file interactively if you haven't written it yet.

### Use the agent skills

Three skills work together, all installed by `tools/agent-plugin/`:

- **`/resident:create-app "description"`** — reads the device's `DEVICE-SKILL.md` and the embedded sandbox docs, generates Lua source against the device's actual surface, validates it locally, writes to `device-apps/<slug>.lua`. Supports `--device-skill <path>` if `DEVICE-SKILL.md` isn't in cwd.
- **`/resident:validate-app <path>`** — runs the Lua app through a local `lua` interpreter under a permissive stub harness derived from `DEVICE-SKILL.md`. Catches syntax errors, missing lifecycle (`init` / `on_tick` / `on_event`), and obvious runtime bugs (nil dereferences) before pushing.
- **`/resident:push-app`** — accepts either a Lua file or a natural-language description. With a file, sends it to the relay. With a description, chains through `create-app` + `validate-app` first.

### The default backend

`resident.inanimate.tech` is the public relay. Read the device ID off the device's display, then:

```bash
/resident:push-app --device-id <id> path/to/app.lua

# or just describe the app:
/resident:push-app "make the screen flash red on shake"
```

The relay forwards the message to the device's WebSocket. No authentication beyond the device ID (treat it like an API key for development; switch to a longer random ID for anything you actually deploy — see the m5stick-demo's `## Device IDs as auth` note).

To self-host instead, deploy the Cloudflare Worker under `examples/m5stick-demo/server/` and pass `--base-url https://your-worker.example.workers.dev`.

### What the agent should be able to demonstrate

End-to-end: a one-line natural-language request produces validated Lua, pushes it to the device, and the device runs it. The full Resident loop — author, push, run — works on hardware the agent set up themselves four steps ago.

---

## Falling back

If a later step regresses earlier work, the preserved sub-projects in `examples/adafruit-esp32-s2-feather/` show the pattern: keep each phase as its own buildable PlatformIO project (not just stashed source files), so the full set of decisions, `platformio.ini` deps, and partition layouts stay intact and re-flashable. Drop back to the last-known-good project, confirm the hardware or Resident layer itself is fine, and isolate the regression to the new layer.
