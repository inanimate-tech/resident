# Adafruit ESP32-S2 TFT Feather example

Three PlatformIO sub-projects for the [Adafruit ESP32-S2 TFT Feather](https://learn.adafruit.com/adafruit-esp32-s2-tft-feather/overview) (Product 5300 — single-core ESP32-S2, native USB-C, 240×135 ST7789 TFT, LC709203 fuel gauge). Each one is independently flashable; pick the one that matches what you're trying to verify.

| Directory | What it is |
|---|---|
| **[`device/`](./device/)** | **The full example.** Resident sandbox + three hardware Lua modules (`screen.*`, `led.*`, `battery.*`) + an auto-loaded device-ID splash app. Flash this if you want the working Feather demo. |
| **[`device-minimal-resident/`](./device-minimal-resident/)** | Smaller reference: Resident connects to the relay and runs Lua, but **no hardware Lua modules** are registered. Useful as a "is the Resident stack itself working?" baseline if `device/` misbehaves. |
| **[`device-no-resident/`](./device-no-resident/)** | Pure hardware bring-up — no Wi-Fi, no Lua, no Resident. The smallest project that exercises the chip, USB serial, LED, NeoPixel, I2C bus, LC709203, and TFT. Use this to confirm the hardware itself is fine. |

Build and flash any of them the usual way:

```bash
cd device           # or device-minimal-resident, or device-no-resident
pio run -t upload
pio device monitor
```

If something regresses in `device/`, the other two are known-good fallbacks for narrowing down where the problem is.

For the full walkthrough of how a new board moves through these phases — and the decisions you'll face along the way — read [`docs/start-building.md`](../../docs/start-building.md).
