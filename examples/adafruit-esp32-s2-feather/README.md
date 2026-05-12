# Adafruit ESP32-S2 TFT Feather example

Two PlatformIO sub-projects for the [Adafruit ESP32-S2 TFT Feather](https://learn.adafruit.com/adafruit-esp32-s2-tft-feather/overview) (Product 5300 — single-core ESP32-S2, native USB-C, 240×135 ST7789 TFT, LC709203 fuel gauge):

| Directory | What it is |
|---|---|
| **[`device-no-resident/`](./device-no-resident/)** | Step 1 — hardware bring-up only. No Wi-Fi, no Lua, no Resident. The smallest possible project that exercises the chip, USB serial, LED, NeoPixel, I2C bus, LC709203, and TFT, so you can isolate hardware/library problems before the Resident stack lands on top. |
| **[`device/`](./device/)** | Step 2 — Resident integration. Keeps the bring-up initialisation and layers Wi-Fi (via WiFiManager's captive portal), the Resident Lua sandbox, WebSocket transport to the relay, and a TFT-backed `StatusDisplay` on top. Lua apps received via `push-app` run on the device. |

Build and flash either directory the usual way:

```bash
cd device           # or: cd device-no-resident
pio run -t upload
pio device monitor
```

If something regresses in `device/`, `device-no-resident/` is a known-good fallback you can flash to confirm the hardware is fine.

For the full walkthrough of how a new board moves through these two phases — and the decisions you'll face along the way — read [`docs/start-building.md`](../../docs/start-building.md).
