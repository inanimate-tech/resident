# Adafruit ESP32-S2 Feather — bring-up

A hardware bring-up sketch for the [Adafruit ESP32-S2 Feather](https://learn.adafruit.com/adafruit-esp32-s2-feather/overview). It exercises the board's core peripherals so you know everything works **before** integrating the [Resident](https://github.com/inanimate-tech/resident) Lua sandbox. Resident integration is a separate, follow-on phase.

## What it tests

- **Chip identity** — prints the ESP32-S2 model, core count, CPU MHz, flash and PSRAM sizes over USB serial.
- **Red user LED** — heartbeat blink on `LED_BUILTIN` every 500 ms.
- **Onboard NeoPixel** — cycles red → green → blue → off, also at 500 ms. Drives `NEOPIXEL_POWER` HIGH first so the pixel actually gets power.
- **I2C bus scan** — drives `I2C_POWER` HIGH, then scans 0x01–0x7F and prints whatever responds. The onboard LC709203 should show up at `0x0B`. The BME280 variant of the board also shows `0x77`.
- **Battery fuel gauge (LC709203)** — initializes the chip and prints cell voltage + state-of-charge once per second.

## How to flash

You'll need the [PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html). Then, from this directory:

```bash
pio run -t upload
pio device monitor
```

## Expected output

After a 2 s pause for USB enumeration:

```
=== Adafruit ESP32-S2 Feather bring-up ===
Chip:  ESP32-S2, 1 core(s) @ 240 MHz
Flash: 4096 KB
PSRAM: 2048 KB
I2C scan:
  device at 0x0B
LC709203 OK
[1234 ms] heartbeat #1 — battery: 4.12V (87%)
[2234 ms] heartbeat #2 — battery: 4.12V (87%)
...
```

You should also see the red LED toggling at 2 Hz and the NeoPixel cycling colors at the same rate.

## Known quirks

- The 2-second startup delay is intentional. The ESP32-S2's native USB takes that long to enumerate on the host; without it you miss the boot banner.
- On first flash after a CircuitPython install, the board may not auto-reset into the bootloader. Hold `BOOT`, tap `RESET`, release `BOOT`, then re-run `pio run -t upload`.
- The LC709203 percentage takes ~30 s to converge — the first few readings can be misleading.
- `LC709203F_APA_500MAH` is the library's pack-size enum for a 500 mAh LiPo. If you're using a different cell, edit `setPackSize(...)` in `src/main.cpp` and re-flash.

## Next step

This directory is bring-up only. The Resident sandbox integration — Lua app runtime, WebSocket transport, deviceId discovery, a `DEVICE-SKILL.md` — will follow in a later commit set.
