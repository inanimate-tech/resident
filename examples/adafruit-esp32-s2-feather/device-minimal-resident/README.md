# Adafruit ESP32-S2 TFT Feather — Resident integration

Step 2 of the two-step walkthrough in [`docs/start-building.md`](../../../docs/start-building.md). Builds on the hardware bring-up in [`../device-no-resident/`](../device-no-resident/) and layers the [Resident](https://github.com/inanimate-tech/resident) Lua sandbox on top.

## What it does

- Initialises the same hardware as the bring-up project (TFT, NeoPixel, LC709203, I2C bus).
- Hands off to `Resident::Sandbox`, which owns:
  - Wi-Fi (WiFiManager captive portal on first boot, persisted to NVS).
  - Time sync (ezTime).
  - WebSocket transport to `resident.inanimate.tech` via Courier.
  - Lua sandbox lifecycle and routing of inbound `app` / `shader` / `app_event` messages.
- Draws status on the TFT via a custom `TFTStatusDisplay`. Once the relay opens a WS, the 8-character device ID is shown in big green text — that's what you push apps to.
- The NeoPixel turns green once connected (yellow otherwise); the red LED keeps blinking at 2 Hz so you can tell the firmware is alive.

## How to flash

You'll need the [PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html). Then, from this directory:

```bash
pio run -t upload
pio device monitor
```

## Wi-Fi setup (first boot only)

On first boot the device hosts an open AP named something like `Resident feather-tft XXXX`. Join it from a phone, the captive portal pops, enter your real Wi-Fi credentials. Saved to NVS — every subsequent boot uses the saved credentials silently.

## Pushing apps

Read the device ID off the TFT, then:

```bash
# from the resident repo root, or anywhere that has push-app:
./examples/m5stick-demo/send-app.sh --device-id <id> path/to/your/app.lua
```

Or, with the agent plugin installed, `/resident:push-app` does the same thing from inside Claude Code.

## What Lua apps can do today

Only the **sandbox-generic** surface — `log.*`, `time.*`, `kv.*`, math, shader globals. Hardware Lua modules (`screen.*` for the TFT, `led.*` for the NeoPixel, `battery.*` for the LC709203) are not exposed yet — they're step 3. Apps that reference `screen` (e.g. the m5stick-demo's `hello.lua`) will hit a Lua runtime error like `attempt to index a nil value (global 'screen')` and the rest of the app won't run.

## Why a custom `partitions.csv`

Resident + WiFiManager + Courier + ArduinoJson + Esp32Lua + ezTime push the binary toward 1.2 MB. The default 4 MB layout splits that across `app0` and a big SPIFFS in a way that leaves no head-room. The bundled `partitions.csv` gives `app0` 2.5 MB and `spiffs` 1.4 MB — comfortable.

## Why a Resident symlink in `lib_deps`

`symlink://../../..` resolves to the repo root, where Resident lives. Means the example tracks the in-tree Resident source — change a Resident header, re-`pio run`, and your firmware picks it up immediately. The trade-off is a PIO LDF quirk that hides `Adafruit BusIO` (a transitive dep of LC709203F + ST7789); the explicit `adafruit/Adafruit BusIO` line in `lib_deps` re-adds it.

## Falling back

If something here regresses your hardware, [`../device-no-resident/`](../device-no-resident/) is a known-good baseline — flash it to confirm the board itself is fine and the issue is in the Resident layer.
