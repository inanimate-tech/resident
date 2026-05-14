# Adafruit ESP32-S2 Feather Bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a PlatformIO-based hardware bring-up example for the Adafruit ESP32-S2 Feather under `examples/adafruit-esp32-s2-feather/`. No Resident integration — bring-up only.

**Architecture:** Three files, flat layout: `platformio.ini`, `src/main.cpp`, `README.md`. Two iterative implementation tasks (scaffold + boot banner, then peripherals) plus a docs task. Verification per task is `pio run` (the project compiles cleanly with the pinned platform and lib versions). Runtime verification requires the physical board and is out of scope for the implementer.

**Tech Stack:** PlatformIO (`espressif32@6.12.0`), arduino-esp32 framework, Adafruit_NeoPixel, Adafruit_LC709203F.

**Spec:** `docs/superpowers/specs/2026-05-12-adafruit-esp32-s2-feather-bringup-design.md`

**Verification note:** This is firmware. The compile check (`pio run`) catches: missing libs, wrong board ID, wrong macro names, type errors. It does NOT validate runtime behavior — that needs the actual board powered up over USB-C. Each task ends with a `pio run` gate; the implementer should not claim success without a clean compile. If `pio` isn't installed in the environment, the implementer should escalate (`NEEDS_CONTEXT`) rather than skipping the check.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `examples/adafruit-esp32-s2-feather/platformio.ini` | PlatformIO project config: board, framework, build flags, lib deps. | Create. |
| `examples/adafruit-esp32-s2-feather/src/main.cpp` | The bring-up sketch: chip stats banner, LED heartbeat, NeoPixel cycle, I2C scan, LC709203 read. | Create in Task 1 (scaffold), grow in Task 2 (peripherals). |
| `examples/adafruit-esp32-s2-feather/README.md` | User-facing how-to-flash-and-what-to-expect doc. | Create. |

---

## Task 1: Scaffold the PlatformIO project + boot banner

**Files:**
- Create: `examples/adafruit-esp32-s2-feather/platformio.ini`
- Create: `examples/adafruit-esp32-s2-feather/src/main.cpp`

Goal: get a buildable project that prints chip stats and a heartbeat over native USB serial. No peripherals yet — proves the platform+board+USB-CDC are correctly wired before we add libraries.

- [ ] **Step 1.1: Create the directory**

From the repo root (`/Users/matt/code/resident`):

```bash
mkdir -p examples/adafruit-esp32-s2-feather/src
```

- [ ] **Step 1.2: Write platformio.ini**

Create `examples/adafruit-esp32-s2-feather/platformio.ini` with exactly this content:

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

(The lib_deps are present from the start so Task 2 doesn't need to re-edit `platformio.ini`. PlatformIO won't complain about unused libs in lib_deps — they're only fetched when first included.)

- [ ] **Step 1.3: Write the scaffold main.cpp**

Create `examples/adafruit-esp32-s2-feather/src/main.cpp` with exactly this content:

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for USB-CDC enumeration on the host.

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 Feather bring-up ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() / 1024));
  Serial.printf("PSRAM: %lu KB\n",
                (unsigned long)(ESP.getPsramSize() / 1024));
}

void loop() {
  static uint32_t lastHeartbeat = 0;
  static uint32_t count = 0;
  uint32_t now = millis();
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    count++;
    Serial.printf("[%lu ms] heartbeat #%lu\n",
                  (unsigned long)now, (unsigned long)count);
  }
}
```

- [ ] **Step 1.4: Verify compile**

```bash
cd /Users/matt/code/resident/examples/adafruit-esp32-s2-feather
pio run
```

Expected: PlatformIO downloads the platform + framework (first run only — can take a few minutes), compiles, and prints a "SUCCESS" line at the end. No errors, no warnings beyond standard library noise. The lib_deps download (NeoPixel, LC709203F, BusIO) happens but they're not yet `#include`d — that's fine.

If `pio` is not installed: STOP and report NEEDS_CONTEXT.

If compile fails: read the error. Most likely causes:
- Wrong board ID → check spelling: `adafruit_feather_esp32s2` (no hyphen, underscores).
- Platform version unavailable → try `espressif32@~6.12.0` or report BLOCKED.

- [ ] **Step 1.5: Commit**

```bash
cd /Users/matt/code/resident
git add examples/adafruit-esp32-s2-feather/platformio.ini \
        examples/adafruit-esp32-s2-feather/src/main.cpp
git commit -m "feat(examples): scaffold Adafruit ESP32-S2 Feather bring-up project"
```

---

## Task 2: Add LED, NeoPixel, I2C scan, and LC709203 battery monitor

**Files:**
- Modify: `examples/adafruit-esp32-s2-feather/src/main.cpp`

Goal: replace the scaffold sketch with the full bring-up. All peripherals added in one task because they're small, independent, and tightly related.

- [ ] **Step 2.1: Replace src/main.cpp**

Overwrite `examples/adafruit-esp32-s2-feather/src/main.cpp` with exactly this content:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LC709203F.h>

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static Adafruit_LC709203F battery;
static bool batteryReady = false;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for USB-CDC enumeration on the host.

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 Feather bring-up ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() / 1024));
  Serial.printf("PSRAM: %lu KB\n",
                (unsigned long)(ESP.getPsramSize() / 1024));

  pinMode(LED_BUILTIN, OUTPUT);

  // Onboard NeoPixel: data on PIN_NEOPIXEL, but power gated by NEOPIXEL_POWER.
  // Drive HIGH before talking to the pixel.
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  pixel.begin();
  pixel.setBrightness(20);
  pixel.show();

  // STEMMA QT + onboard LC709203 share an I2C bus gated by I2C_POWER.
  pinMode(I2C_POWER, OUTPUT);
  digitalWrite(I2C_POWER, HIGH);
  delay(10);
  Wire.begin();

  Serial.println("I2C scan:");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  (no devices)");

  if (battery.begin()) {
    battery.setPackSize(LC709203F_APA_500MAH);
    batteryReady = true;
    Serial.println("LC709203 OK");
  } else {
    Serial.println("LC709203 not found — continuing without battery readings");
  }
}

void loop() {
  static const uint32_t COLORS[] = {0xFF0000, 0x00FF00, 0x0000FF, 0x000000};
  static uint32_t lastBlink = 0;
  static uint32_t lastHeartbeat = 0;
  static uint8_t colorIndex = 0;
  static bool ledOn = false;
  static uint32_t count = 0;

  uint32_t now = millis();

  if (now - lastBlink >= 500) {
    lastBlink = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn);
    pixel.setPixelColor(0, COLORS[colorIndex]);
    pixel.show();
    colorIndex = (colorIndex + 1) % 4;
  }

  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    count++;
    if (batteryReady) {
      Serial.printf("[%lu ms] heartbeat #%lu — battery: %.2fV (%.0f%%)\n",
                    (unsigned long)now, (unsigned long)count,
                    battery.cellVoltage(), battery.cellPercent());
    } else {
      Serial.printf("[%lu ms] heartbeat #%lu\n",
                    (unsigned long)now, (unsigned long)count);
    }
  }
}
```

Notes for the implementer:
- The pin constants (`PIN_NEOPIXEL`, `NEOPIXEL_POWER`, `LED_BUILTIN`, `I2C_POWER`) come from the arduino-esp32 board variant for `adafruit_feather_esp32s2` — no `#define` needed.
- `Wire` uses the board variant's default `SDA`/`SCL`. No need to pass pins to `Wire.begin()`.
- `LC709203F_APA_500MAH` is one of the library's pack-size enums; user can edit to match their actual battery.

- [ ] **Step 2.2: Verify compile**

```bash
cd /Users/matt/code/resident/examples/adafruit-esp32-s2-feather
pio run
```

Expected: compiles successfully. PlatformIO pulls down NeoPixel + LC709203F + BusIO this time (if not already cached). Output ends with "SUCCESS".

Common failure modes:
- `'PIN_NEOPIXEL' was not declared in this scope` → the board variant didn't expose it. Verify the board ID in `platformio.ini` is `adafruit_feather_esp32s2`. (Some older platform versions used `featheresp32-s2` — incompatible variant.)
- `'LC709203F_APA_500MAH' was not declared` → library version mismatch. Check `lib_deps` pin.

- [ ] **Step 2.3: Commit**

```bash
cd /Users/matt/code/resident
git add examples/adafruit-esp32-s2-feather/src/main.cpp
git commit -m "feat(examples): add full peripherals to ESP32-S2 Feather bring-up"
```

---

## Task 3: README

**Files:**
- Create: `examples/adafruit-esp32-s2-feather/README.md`

Goal: a user opening this directory understands what it does, how to flash, what they should see, and the known quirks.

- [ ] **Step 3.1: Write the README**

Create `examples/adafruit-esp32-s2-feather/README.md` with exactly this content:

````markdown
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
````

(The outer fence is four backticks to allow the inner triple-backtick code blocks to render correctly inside this plan. The actual file should start with `# Adafruit ESP32-S2 Feather — bring-up` directly — no outer fence.)

- [ ] **Step 3.2: Re-read the README**

Read `examples/adafruit-esp32-s2-feather/README.md` back. Confirm:
- Title is `# Adafruit ESP32-S2 Feather — bring-up` (no four-backtick fence around the whole file).
- The five test bullets are present (chip, LED, NeoPixel, I2C, battery).
- Flash + monitor commands use `pio run -t upload` and `pio device monitor`.
- Expected output block is present and matches what `main.cpp` actually prints.
- Known quirks list has four items.
- Final "Next step" paragraph explicitly says Resident integration is a later phase.

- [ ] **Step 3.3: Commit**

```bash
cd /Users/matt/code/resident
git add examples/adafruit-esp32-s2-feather/README.md
git commit -m "docs(examples): add README for ESP32-S2 Feather bring-up"
```

---

## Self-Review

**Spec coverage (against `docs/superpowers/specs/2026-05-12-adafruit-esp32-s2-feather-bringup-design.md`):**

- Flat directory layout (`platformio.ini`, `src/main.cpp`, `README.md`) — Tasks 1, 2, 3.
- `platformio.ini` with `espressif32@6.12.0`, `adafruit_feather_esp32s2`, USB-CDC flag, NeoPixel + LC709203F lib deps — Task 1, Step 1.2.
- Boot banner with chip stats — Task 1, Step 1.3; preserved in Task 2.
- LED heartbeat (500 ms) — Task 2, Step 2.1.
- NeoPixel cycle (R→G→B→off, 500 ms) — Task 2, Step 2.1.
- `NEOPIXEL_POWER` driven HIGH before pixel.begin() — Task 2, Step 2.1.
- I2C bus scan (0x01–0x7F) — Task 2, Step 2.1.
- `I2C_POWER` driven HIGH before Wire.begin() — Task 2, Step 2.1.
- LC709203 init with `setPackSize(LC709203F_APA_500MAH)` and graceful "not found" handling — Task 2, Step 2.1.
- millis()-based scheduling (no `delay()` in `loop()`) — Task 2, Step 2.1.
- README with the five required sections — Task 3, Step 3.1.
- No partitions.csv, no WiFi, no device-apps/, no server/ — confirmed (no task creates them).

No spec gaps.

**Placeholder scan:** Every code block is complete. Every command is exact. Every expected output is concrete. No "fill in" or "add appropriate".

**Type consistency:** Identifier names match across tasks:
- `pixel`, `battery`, `batteryReady` introduced in Task 2 and used consistently.
- Function names `setup`/`loop` are standard Arduino.
- Pin constants (`LED_BUILTIN`, `PIN_NEOPIXEL`, `NEOPIXEL_POWER`, `I2C_POWER`) used identically.
- The platform pin `espressif32@6.12.0` and board ID `adafruit_feather_esp32s2` match between `platformio.ini`, README, and the spec.
