# Adafruit ESP32-S2 TFT Feather — Resident (full)

Step 2 of the [`docs/start-building.md`](../../../docs/start-building.md) walkthrough. Builds on the hardware bring-up in [`../device-no-resident/`](../device-no-resident/) and layers the [Resident](https://github.com/inanimate-tech/resident) Lua sandbox plus three hardware Lua modules on top.

For the smaller intermediate version that brings Resident up *without* exposing hardware to Lua, see [`../device-minimal-resident/`](../device-minimal-resident/).

## What it does

- Initialises the same hardware as the bring-up project (TFT, NeoPixel, LC709203, I2C bus).
- Hands off to `Resident::Device`, which owns:
  - Wi-Fi (WiFiManager captive portal on first boot, persisted to NVS).
  - Time sync (ezTime).
  - WebSocket transport to `resident.inanimate.tech` via Courier.
  - Lua sandbox lifecycle and routing of inbound `app` / `shader` / `app_event` messages.
- Registers three hardware drivers with Resident as Lua modules:
  - **`screen.*`** — the TFT, backed by a 135×240 `GFXcanvas16` framebuffer. Double-buffered: draw with `clear`/`text`/`fill_rect`/etc., then `screen.flip()` to push the frame.
  - **`led.*`** — the onboard NeoPixel. `set(r,g,b)`, `set_brightness(n)`, `off()`.
  - **`battery.*`** — the LC709203F fuel gauge. `voltage()`, `percent()`, `present()`.
- The TFT display is in portrait orientation (135×240, USB-C at the bottom).
- Once connected, auto-loads a tiny Lua app that paints the device ID big and green and sets the NeoPixel green. Any real app sent via `push-app` replaces it.

## Lua surface for app authors

See [`DEVICE-SKILL.md`](./DEVICE-SKILL.md) for the complete Lua API documentation. The `/resident:create-app` skill reads this file to generate Lua apps for the device.

## How to flash

You'll need the [PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html). Then, from this directory:

```bash
pio run -t upload
pio device monitor
```

## Wi-Fi setup (first boot only)

On first boot the device hosts an open AP named something like `Resident feather-tft XXXX`. Join from a phone, the captive portal pops, enter your real Wi-Fi credentials. Saved to NVS — every subsequent boot connects silently.

## Pushing apps

Read the device ID off the TFT, then either:

```bash
# Using m5stick-demo's send-app.sh (until the Feather grows its own):
./../../m5stick-demo/send-app.sh --device-id <id> path/to/your/app.lua
```

Or via Claude Code:

```
/resident:push-app --device-id <id> path/to/app.lua
```

Or describe an app and let `/resident:create-app` write it using this directory's `DEVICE-SKILL.md`.

## Indicators (what's on the screen / pixel / LED)

| State | TFT | NeoPixel | Red LED |
|---|---|---|---|
| Booting | "Resident / ESP32-S2 TFT Feather / WiFi" (yellow) | Blue | Blinks at 2 Hz |
| Captive portal active (first boot) | "WiFi" / "Connecting" (yellow) | Blue | Blinks at 2 Hz |
| Connected, no app loaded yet | "Resident / feather-tft / Device ID: XXXXXXXX" (green) | Green | Blinks at 2 Hz |
| Lua app running | Whatever the app draws | Whatever the app sets | Blinks at 2 Hz (firmware alive) |

The red LED blink is always-on as a "firmware is running" indicator independent of anything Lua does.

## Falling back

If the Resident integration regresses your hardware bring-up:

- [`../device-no-resident/`](../device-no-resident/) — pure hardware bring-up, no Resident at all. Known-good baseline that confirms the board is fine.
- [`../device-minimal-resident/`](../device-minimal-resident/) — Resident connects to the relay, but no hardware Lua modules are exposed. Useful if you suspect a driver bug.
