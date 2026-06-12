# m5stick-grove-vision-ai

[Resident](../..) example: an M5StickS3 with a Seeed **Grove Vision AI V2**
camera module on the Grove port. The Vision AI module runs a SenseCraft model
entirely on-device (Himax WiseEye2: Cortex-M55 + Ethos-U55 NPU) and the
M5Stick polls results over I2C, feeding them into the Lua sandbox as events
and a pollable `vision.*` module — so hot-reloadable Lua apps can react to
what the camera sees.

The driver is **model-agnostic**: flash a different SenseCraft model onto the
camera module and the same firmware keeps working; only the result kind
(`boxes` / `classes` / `pose` / `points`) changes. The driver also logs every
frame verbosely to serial so you can eyeball what an unfamiliar model emits.

## Hardware

| Component | Details |
|---|---|
| M5StickS3 | ESP32-S3, 1.14" 135×240 LCD, Grove port (I2C: SDA=GPIO9, SCL=GPIO10) |
| Grove Vision AI Module V2 | Himax WiseEye2 HX6538; SSCMA/I2C host interface (addr 0x62) |
| OV5647 camera | CSI ribbon to the Vision AI module |

Wiring: camera ribbon → Vision AI module, Grove cable → M5StickS3 Grove
port. The Grove port powers the module; USB on the module is only needed
while flashing models.

## Flash a model onto the camera module

1. Open [SenseCraft AI](https://sensecraft.seeed.cc/ai/home) in Chrome/Edge.
2. Connect the Vision AI module via USB-C, pick **Grove Vision AI V2**.
3. Choose a model (person detection, face detection, gesture
   rock-paper-scissors, human pose, …) and deploy.
4. Unplug USB; the module now runs that model standalone.

## Build and flash the M5Stick

```sh
cd device
pio run -e m5sticks3 -t upload
pio device monitor          # watch [vision] logs (115200 baud)
pio test -e native          # vision_frame helper unit tests (no hardware)
```

On boot the device joins WiFi (Courier config portal on first run), connects
to the Resident relay, and shows its device ID. Push an app:

```sh
./send-app.sh --device-id <id-from-screen> device-apps/presence.lua
```

## Demo apps

- `device-apps/presence.lua` — beep + flash when anything is detected
  (any detection model).
- `device-apps/tracker.lua` — draw the best detection's box live (detection
  or pose models).
- `device-apps/skeleton.lua` — stick figure from the 17 pose keypoints
  (human pose model).
- `device-apps/rps-monitor.lua` — live view of what the gesture model sees:
  every box, the best detection's label, raw target + score (validation aid
  for the rock-paper-scissors model).
- `device-apps/rps-game.lua` — rock/paper/scissors against the device, best
  of three: countdown with beeps, capture window, split-screen reveal, score
  pips and jingles (gesture model).
- plus the generic m5stick apps (`hello.lua`, `bounce.lua`,
  `buttons-buzzer.lua`).

The camera apps mirror the x axis so the device screen behaves like a mirror.

The Lua surface (screen/imu/buzzer/button/vision) is documented in
[DEVICE-SKILL.md](DEVICE-SKILL.md).
