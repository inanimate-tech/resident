# Adafruit ESP32-S2 TFT Feather — bring-up

A hardware bring-up sketch for the [Adafruit ESP32-S2 TFT Feather](https://learn.adafruit.com/adafruit-esp32-s2-tft-feather/overview) (Product 5300 — has an onboard 1.14" 240×135 ST7789 TFT). It exercises the board's core peripherals so you know everything works **before** integrating the [Resident](https://github.com/inanimate-tech/resident) Lua sandbox. Resident integration is a separate, follow-on phase.

## What it tests

- **Chip identity** — prints the ESP32-S2 model, core count, CPU MHz, flash and PSRAM sizes over USB serial and on the TFT.
- **Red user LED** — heartbeat blink on `LED_BUILTIN` every 500 ms.
- **Onboard NeoPixel** — cycles red → green → blue → off, also at 500 ms.
- **TFT display** — 240×135 ST7789 in landscape (USB-C on the right). Shows the static chip info on boot and a live battery/heartbeat/uptime block updated every second.
- **I2C bus scan** — drives `TFT_I2C_POWER` HIGH (this pin powers both the TFT and the I2C bus), then scans 0x01–0x7F and prints whatever responds. The onboard LC709203 should show up at `0x0B` when a battery is plugged in.
- **Battery fuel gauge (LC709203)** — initializes the chip and prints cell voltage + state-of-charge once per second.
- **Status splash** — at the end of setup, the NeoPixel turns solid for 2 seconds as a visible "ready" signal:
  - **Green** — full bring-up, including the LC709203 battery monitor.
  - **Yellow** — chip booted and the I2C bus is powered, but the LC709203 didn't respond. Usually this just means **no LiPo battery is connected** (the LC709203 is powered by VBAT, so without a battery it's invisible on I2C). Plug a battery in and re-power to see green.

## How to flash

You'll need the [PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html). Then, from this directory:

```bash
pio run -t upload
pio device monitor
```

## Expected output

After a 2 s pause for USB enumeration:

```
=== Adafruit ESP32-S2 TFT Feather bring-up ===
Chip:  ESP32-S2, 1 core(s) @ 240 MHz
Flash: 4096 KB
PSRAM: 2048 KB
I2C scan:
  device at 0x0B
LC709203 OK
READY
[1234 ms] heartbeat #1 — battery: 4.12V (87%)
[2234 ms] heartbeat #2 — battery: 4.12V (87%)
...
```

After the boot banner, the NeoPixel holds solid green for 2 s (full success) or solid yellow (no LC709203 — usually just means no battery is plugged in). After that, the red LED toggles at 2 Hz and the NeoPixel cycles colors at the same rate. The TFT shows chip info up top and a live battery / heartbeat / uptime block at the bottom.

## Known quirks

- **First flash only** — if the board shipped with CircuitPython (or any non-Arduino firmware that doesn't speak the 1200-baud touch reset), the very first `pio run -t upload` won't auto-enter the bootloader. Hold `BOOT`, tap `RESET`, release `BOOT`, then re-run. **Every subsequent upload is hands-free** — the `board_upload.use_1200bps_touch` flag in `platformio.ini` plus USB-CDC-on-boot lets esptool trigger the bootloader over native USB.
- The 2-second startup delay in `setup()` is intentional. The ESP32-S2's native USB takes that long to enumerate on the host; without it you miss the boot banner.
- **Amber CHG LED flashing fast (5–10 Hz) with no battery is normal** — it's the charger IC reporting "no battery to charge", not a firmware fault. It stops flashing once a LiPo is connected.
- The LC709203 percentage takes ~30 s to converge — the first few readings can be misleading.
- `LC709203F_APA_500MAH` is the library's pack-size enum for a 500 mAh LiPo. If you're using a different cell, edit `setPackSize(...)` in `src/main.cpp` and re-flash.

## Next step

This directory is bring-up only — no networking, no Lua, no Resident. Once you've confirmed the hardware works here, move on to the [Resident integration in `../device/`](../device/), which keeps this initialisation and layers Wi-Fi + the Resident Lua sandbox + WebSocket transport on top. See [`docs/start-building.md`](../../../docs/start-building.md) for the full two-step walkthrough.
