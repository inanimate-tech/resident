# Adafruit ESP32-S2 Feather: hardware bring-up

## Problem

We want to add the Adafruit ESP32-S2 Feather as a target for the
Resident sandbox. Project convention: before wiring a new board into
the sandbox, prove the bare hardware works under PlatformIO first.
This spec is just that bring-up — no Resident integration yet.

## Hardware (relevant subset)

Adafruit ESP32-S2 Feather (Product ID 5000, non-TFT, non-Reverse-TFT
revisions):

- ESP32-S2 single-core, 240 MHz Tensilica.
- 4 MB flash, 2 MB PSRAM.
- Native USB (USB-C connector).
- LiPo charging circuit.
- LC709203F fuel gauge over I2C at address `0x0B`.
- Onboard red user LED on `LED_BUILTIN` (GPIO 13).
- Onboard NeoPixel: data on `PIN_NEOPIXEL`, power-enable on
  `NEOPIXEL_POWER` (must be driven HIGH).
- STEMMA QT (I2C) on `SDA`/`SCL`, with a power-enable pin
  `I2C_POWER` (must be driven HIGH to power the bus AND the onboard
  LC709203). All symbolic constants come from the `arduino-esp32`
  board variant for `adafruit_feather_esp32s2` — no hardcoded GPIO
  numbers needed in firmware.

Reference: https://learn.adafruit.com/adafruit-esp32-s2-feather/overview

## Design

### Directory layout

Flat, no `device/` wrapper. There's only one thing in this example
today; future Resident integration can re-organize if needed.

```
examples/adafruit-esp32-s2-feather/
├── README.md
├── platformio.ini
└── src/
    └── main.cpp
```

### platformio.ini

Single env, mirroring the platform pin from `m5stick-demo` so
examples stay consistent.

```ini
[env:adafruit_feather_esp32s2]
platform = espressif32@6.12.0
board = adafruit_feather_esp32s2
framework = arduino
monitor_speed = 115200
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
    adafruit/Adafruit NeoPixel@^1.12.0
    adafruit/Adafruit LC709203F@^1.3.4
```

The `ARDUINO_USB_CDC_ON_BOOT=1` flag routes `Serial` through the
board's native USB. Without it, `Serial` goes to UART0, which has no
pin broken out on this board — no serial output visible over USB-C.

### src/main.cpp

~100 lines. Uses board-variant constants (`LED_BUILTIN`,
`PIN_NEOPIXEL`, `NEOPIXEL_POWER`, `SDA`, `SCL`, `I2C_POWER`) only.

**setup():**
1. `Serial.begin(115200)` and `delay(2000)` (lets the host enumerate
   the native USB serial endpoint before we print anything).
2. Print a banner and chip stats: `ESP.getChipModel()`,
   `ESP.getChipCores()`, `ESP.getCpuFreqMHz()`,
   `ESP.getFlashChipSize()`, `ESP.getPsramSize()`.
3. Configure `LED_BUILTIN` as output.
4. Configure `NEOPIXEL_POWER` as output, drive HIGH; initialize the
   NeoPixel; set brightness ≈ 20/255.
5. Configure `I2C_POWER` as output, drive HIGH; `delay(10)` for the
   bus to settle.
6. `Wire.begin()` then scan addresses 0x01..0x7F. Print each
   responder. Expected: at minimum `0x0B` (LC709203). The BME280
   variant additionally shows `0x77`.
7. Initialize LC709203 via `Adafruit_LC709203F::begin()`. If it
   returns true, call `setPackSize(LC709203F_APA_500MAH)`. If false,
   log "LC709203 not found — continuing without battery readings" and
   keep going. **Do not lock the board.**

**loop():**
- Every 500 ms: toggle `LED_BUILTIN`; cycle NeoPixel through
  red → green → blue → off.
- Every 1000 ms: print one line containing `millis()`, a heartbeat
  counter, and (if LC709203 initialized successfully) the battery's
  `cellVoltage()` (V) and `cellPercent()` (%). If LC709203 isn't
  available, omit the battery fields.

Use millis()-based scheduling (no `delay()` in `loop()`), so the two
schedules don't block each other.

### README.md

One page. Sections:

1. **What this is.** One-paragraph description of the board with a
   link to the Adafruit overview. Note that this directory is just
   the hardware bring-up; Resident sandbox integration comes later.
2. **What it tests.** Bulleted list of the five checks (chip
   identity, red LED, NeoPixel + power-enable, I2C bus + power-enable,
   LC709203).
3. **How to flash.**
   ```bash
   cd examples/adafruit-esp32-s2-feather
   pio run -t upload
   pio device monitor
   ```
4. **Expected output.** A short sample of the serial banner +
   heartbeat lines so a user knows what success looks like.
5. **Known quirks.**
   - 2-second startup delay is intentional (native USB enumeration).
   - On first flash after a CircuitPython install, you may need to
     hold BOOT and tap RESET to force ROM-bootloader mode.
   - LC709203 percentage takes ~30 s to converge — early readings can
     be misleading.
6. **Next step.** One sentence: Resident integration is the follow-on,
   not part of this example.

### Reproducibility constraints

- Pin platform version (`espressif32@6.12.0`) and lib semver-major
  ranges. No floating versions.
- Use board-variant symbolic constants, not literal GPIO numbers, so
  the firmware survives Adafruit hardware revisions.

## Files touched

- `examples/adafruit-esp32-s2-feather/README.md` — new.
- `examples/adafruit-esp32-s2-feather/platformio.ini` — new.
- `examples/adafruit-esp32-s2-feather/src/main.cpp` — new.

No changes to plugin, skills, or other examples.

## Out of scope

- WiFi. Not needed for bring-up; comes with Resident integration.
- Resident sandbox integration. Separate, later phase.
- A `partitions.csv`. The bring-up sketch is tiny; default partition
  layout is fine.
- A `device-apps/` or `server/` directory. Nothing to put in them
  yet — they belong to the Resident-integration phase.
- Battery pack-size configuration UX. Hardcoded `500MAH` default is
  fine for bring-up; users with a different pack edit one line.
- BME280 sensor read (on the BME280 variant). Detection via I2C scan
  is enough — driving the sensor is sandbox-territory.
