# Start building

This guide walks through adding a new device to Resident. It's the path the Adafruit ESP32-S2 TFT Feather example took from "out of the bag" to "running Lua apps over the relay", and the same shape should work for any ESP32-family board you bring.

There are two steps:

1. **Bring up your hardware** — prove the board boots, USB serial works, and you can drive each peripheral you care about. *No Resident yet.* Output of this step is a small PlatformIO project that you can flash and watch the LEDs / screen / sensors respond.

2. **Add Resident** — keep the same project, layer Resident's sandbox, WebSocket transport, and Lua runtime on top. Output of this step is a device that boots, joins your Wi-Fi, opens a WS to the relay, displays its device ID, and accepts Lua apps over `push-app`.

Each step finishes with something you can flash, run, and see working. That matters: if the Resident layer misbehaves, you can flash the bring-up sketch as a sanity check that the hardware is fine.

This guide refers to `examples/adafruit-esp32-s2-feather/` throughout as the worked example. The `device-no-resident/` subdirectory is the end state of step 1; `device/` is the end state of step 2 (Resident bootstrap + the three hardware Lua modules). A third subdirectory, `device-minimal-resident/`, is preserved as an intermediate snapshot — Resident running but with no hardware Lua modules registered yet — useful as a reference and a fallback. All three are buildable PlatformIO projects; flash any of them with `pio run -t upload` from its own directory.

---

## Step 1 — Bring up your hardware

The goal here is **not** to build anything that connects to a network. It's to exercise the chip and each peripheral with the smallest possible PlatformIO project, so you find hardware/library/pin-mapping problems while the surface area is tiny.

### What to write

A `src/main.cpp` that, in `setup()`, prints chip identity over Serial (chip model, cores, MHz, flash, PSRAM) and initialises each peripheral. In `loop()`, it does something visible at a known cadence — blink an LED, cycle a NeoPixel, redraw a screen — so you can tell at a glance whether the firmware is running.

For the Feather TFT, that meant:

- Serial banner with chip stats.
- Red user LED toggling at 2 Hz.
- NeoPixel cycling red → green → blue → off at 2 Hz (after a 2 s splash colour that conveys a yes/no result).
- I2C bus power-on, then a 0x01–0x7F scan that prints whatever responds.
- LC709203 battery fuel gauge init + read.
- A 240×135 ST7789 TFT showing chip info up top and a live battery / heartbeat / uptime block at the bottom.

### Decisions you'll face

- **Which board ID does PlatformIO use?** Adafruit ships several visually-similar boards under the same family name. Check the *exact* PlatformIO board ID — the wrong one gives you wrong default SDA/SCL pins, wrong NeoPixel power pin, etc., and silent misbehaviour (an I2C scan with no devices is hard to attribute to "wrong pins" vs "no devices"). For the TFT Feather it's `adafruit_feather_esp32s2_tft`, not the very similarly-named `adafruit_feather_esp32s2`. When in doubt, read `~/.platformio/packages/framework-arduinoespressif32/variants/<board>/pins_arduino.h` directly — it's the source of truth.

- **USB-CDC on boot.** Native-USB ESP32-S2 / S3 boards need `-DARDUINO_USB_CDC_ON_BOOT=1` so `Serial` writes go out over the USB-C port instead of UART0 (which usually isn't broken out to a pin). Without this you'll think the chip is dead.

- **Hands-free uploads.** Native USB has no DTR/RTS bridge, so esptool needs `board_upload.use_1200bps_touch = true` + `board_upload.wait_for_upload_port = true` to tell the running firmware "reboot to bootloader" via a 1200-baud port open. The very first flash (or any flash on top of CircuitPython, or after a sketch that breaks USB-CDC) still needs the BOOT + RESET button dance; everything after is automatic.

- **Power-gated peripherals.** Many Adafruit boards put the NeoPixel and the I2C bus behind transistor power gates so they can be cut for low-power modes. The variant header declares the active polarity (e.g. `NEOPIXEL_POWER_ON HIGH`) — trust it. *Don't* implement an INPUT-then-invert "polarity sniff" — the pin may sit at the active state at rest from a board pull, and the sniff gives the wrong answer. (The Adafruit demo sketches do this for portability across board revisions, but we know our variant.)

- **Library Dependency Finder mode.** Adafruit BusIO (a transitive dep of most Adafruit sensor libs) `#include`s `SPI.h` without declaring SPI cleanly in metadata. PlatformIO's default chain LDF can't find it. Set `lib_ldf_mode = deep`.

- **What's a "ready" signal on a screen-less board?** If the board has a display, draw chip info on it. If it doesn't, hold a status colour on a NeoPixel for 2 s at the end of `setup()` before the cycling pattern takes over (green = full success, yellow = soft warning like "no battery"). Save red for hard failures.

- **Things that look like firmware bugs but aren't.** The Feather's amber CHG LED flashes at 5–10 Hz when no battery is plugged in — that's the charger IC, not your code. Document those quirks in the README so the next person doesn't go hunting.

### Why this matters before Resident

Resident pulls in WiFi, ezTime, WiFiManager, Courier, ArduinoJson, Esp32Lua, and the in-tree Resident library. That's a *lot* of code surface, and any of those can fail in ways that look like "the board doesn't work". If your bare bring-up sketch already runs cleanly, you know which side of the fence a problem is on.

---

## Step 2 — Add Resident

Now layer Resident on top. The bring-up code stays — TFT init, NeoPixel power, LC709203 init still run in `setup()` before we hand off to Resident — and we add a `Resident::Device` instance that owns the Wi-Fi, time sync, WebSocket transport, and Lua sandbox.

### What to write

In `platformio.ini`:

- Add the in-tree Resident library via `symlink://../..` (relative from the example back to the repo root).
- Add Resident's runtime deps: `git+https://github.com/inanimate-tech/courier.git`, `tzapu/WiFiManager`, `bblanchon/ArduinoJson`, `ropg/ezTime`, `fischer-simon/Esp32Lua`.
- Add a custom `partitions.csv` — Resident + Lua + WiFiManager + Courier eat far more flash than the default 4 MB layout allows for `app0`. The Feather example gives the app 2.5 MB.
- **PIO quirk** — once you add the `symlink://...` line, LDF's auto-resolution of `Adafruit BusIO` (transitive via LC709203F + ST7789) stops working. Re-add `adafruit/Adafruit BusIO@^1.17.0` explicitly. Same kind of symptom as the m5stick-demo's `lib/drivers` quirk.

In `src/main.cpp`:

- Keep the bring-up initialisation block (TFT power, NeoPixel power, I2C power, `Wire.begin()`, `tft.init()`, `tft.setRotation()`, `battery.begin()`).
- Add a `Resident::StatusDisplay` subclass that draws to the TFT. Resident calls `displayText("WiFi")`, `displayText("Connecting")`, `displayText("Connected")`, and finally `displayText(deviceId)` once the WS opens. Render the device ID big and green; render other status strings in yellow at a medium size.
- Build a `Resident::DeviceConfig`: set `deviceType`, `host = "resident.inanimate.tech"`, point `statusDisplay` at your TFT subclass, and (for now) leave `extensions` empty.
- Subclass `Resident::Device`, override `onTransportsWillConnect()` to set the canonical relay path `/devices/<id>` instead of the default `/agents/<type>-agent/<id>` (the relay's URL convention).
- In `loop()`, call `device.loop()` and keep the bring-up's LED toggle + NeoPixel update — green when connected, yellow when not.

### Decisions you'll face

Most of these have a sensible default. The doc lists them so you know what knob you're tweaking, not because every project needs to change them.

- **Which relay endpoint?** The canonical public relay (`resident.inanimate.tech`) is the simplest path and the one `push-app` defaults to. If you want auth, persistence, or richer state, self-host a Cloudflare Worker from `examples/m5stick-demo/server/`. The change is one line: replace `RESIDENT_HOST`.

- **What's the `deviceType` string?** It's the identifier the relay uses for its default WebSocket path (`/agents/<deviceType>-agent/<id>`). When you also override `onTransportsWillConnect()` to use `/devices/<id>` instead, `deviceType` becomes mostly cosmetic — but it's still useful for logs and (eventually) for routing different types of devices on the relay side. The Feather uses `"feather-tft"`.

- **WiFi setup UX.** Courier defaults to a WiFiManager captive portal: on first boot the device hosts an open AP named `Resident <DeviceType> <ID-short>`; you join it on your phone and enter your real SSID + password. Credentials persist in NVS thereafter. If you don't want a captive portal, swap in a hardcoded `WiFi.begin(ssid, pass)` — but the captive portal is friendly for example projects.

- **What does the device show before it's connected?** The TFT StatusDisplay subclass receives short status strings ("WiFi", "Connecting", "Connected"). Decide whether to render them prominently or quietly. The Feather renders them in yellow at size 2; once a real 8-char device ID arrives, it switches to size 3, green.

- **What goes on the screen once connected?** This example loads a tiny built-in Lua app (printing the device ID via `log.info`) so the user immediately sees an interactive demo. A real app sent via `push-app` replaces it. Alternatively, you could draw the device ID directly from C++ and skip the bootstrap app. The bootstrap-app pattern means the user's first impression of "this is a Resident device" is *a Lua app running*, which is the right mental model.

- **Which Lua hardware modules to expose?** This is the largest design decision. The minimum-viable Resident device (preserved at `device-minimal-resident/`) is one that connects and runs Lua, even if that Lua can only `log` and do math. The full version (`device/`) adds three drivers — `screen.*` (TFT), `led.*` (NeoPixel), `battery.*` (LC709203) — and a `DEVICE-SKILL.md` documenting them so `/resident:create-app` can generate Lua against the device's actual surface. Each driver is a separate `Resident::Driver` subclass with a `registerModule()` that binds C++ methods to Lua names. Conventions worth following:
  - One driver per hardware unit. Don't merge `screen` and `led` into one driver just because they share `main.cpp`.
  - Double-buffer the screen via an off-screen canvas (`GFXcanvas16` in `device/`, `M5Canvas` in `m5stick-demo`). `screen.flip()` pushes a single frame in one SPI transfer. Drawing one shape at a time directly to the TFT is too slow for a game loop.
  - On `onAppReset()` (called when a new app is loaded or the current one errors), restore a safe state: clear the canvas, turn the NeoPixel off. Don't leak the previous app's pixels into the next app's first frame.
  - Build the Adafruit GFX / TFT object in `main.cpp` and `init()` it *before* `device.setup()` runs. The driver's `begin()` (canvas alloc, backlight PWM setup) gets called from inside Resident's lifecycle and assumes the hardware is already up.
  - PIO quirk: with `symlink://...` in `lib_deps`, your local `lib/drivers` directory gets skipped by LDF's auto-scan. Add `symlink://lib/drivers` to `lib_deps` explicitly (see `device/platformio.ini`).

- **Partition table.** The Feather has 4 MB flash. The default layout splits this into `app0` (~1.4 MB) and a big SPIFFS, but Resident's stack pushes the binary toward 1.2–1.4 MB, leaving almost no head-room. Custom layout: 2.5 MB app, 1.4 MB SPIFFS. If you add OTA later you'll need `app0` + `app1`; for now single-slot is fine.

### What the user sees end-to-end

1. Flash the firmware via `pio run -t upload`.
2. TFT shows `Resident / ESP32-S2 TFT Feather / WiFi` (yellow).
3. On first boot, the device hosts an open Wi-Fi AP named `Resident feather-tft XXXX`. Join it on a phone, enter real Wi-Fi credentials. (On subsequent boots, this step is skipped — credentials persist in NVS.)
4. TFT cycles through `Connecting`, then `Connected`, then the 8-character device ID in big green text.
5. NeoPixel turns green; red LED keeps blinking at 2 Hz.
6. `push-app --device-id <id> some-app.lua` sends Lua to the device. Resident runs it. Built-in modules (`log`, `time`, `kv`, math) are available; hardware modules will come in step 3.

### What's deliberately not done yet

- No `partitions.csv` reservation for OTA. If you want over-the-air updates later, double the `app` partition and add an `app1`.
- No buttons / IMU / buzzer exposed to Lua — this board doesn't have any. If your board does, the m5stick-demo's `IMUDriver`, `PushButtonsDriver`, and `BuzzerDriver` show how to expose them, including the event-sink machinery for button presses (driver calls `sendEvent("button", ...)` and the sandbox surfaces it to Lua's `on_event`).

### Falling back

Two known-good fallbacks are preserved as separate PlatformIO projects:

- `examples/adafruit-esp32-s2-feather/device-no-resident/` — pure hardware bring-up, no Resident at all. Confirms the board is fine.
- `examples/adafruit-esp32-s2-feather/device-minimal-resident/` — Resident connects to the relay, but no hardware Lua modules. Confirms the Resident stack is fine; useful for isolating driver bugs.

Keeping these as separate projects (not just stashed `main.cpp` files) means the full set of decisions, `platformio.ini` deps, and partition layouts stay intact and re-flashable.
