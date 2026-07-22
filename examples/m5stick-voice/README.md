# M5Stick Voice (push-to-talk)

A push-to-talk audio example for the M5StickC Plus2 / M5StickS3, built on the
[Resident](https://github.com/inanimate-tech/resident) sandbox and its drivers.
**Hold the front button to stream the microphone over a WebSocket.**

> **Milestone 1.** This proves the audio-streaming path with live transcription.
> The device streams 16 kHz PCM to the m5stick-voice server (a Cloudflare Worker)
> which forwards audio to OpenAI Realtime and streams the transcript back to a
> browser viewer. The push-to-talk path itself is composed entirely from
> Resident core primitives (system-button hold, the overlay arbiter,
> `SystemMic` streaming) — there is no Lua-app surface yet, so this example
> ships no `DEVICE-SKILL.md`.

![Browser viewer showing live transcription and FFT strip while the device streams audio](./viewer.png)

## Structure

```
m5stick-voice/
├── device/          # PlatformIO firmware for M5StickC Plus2 / M5StickS3
└── server/          # Cloudflare Worker: OpenAI transcription + live viewer
```

The drivers (display, IMU, buzzer, buttons) are shared with
[`examples/m5stick-demo`](../m5stick-demo/) via a symlink in `platformio.ini` —
there is one canonical copy, in m5stick-demo.

## Build & flash

[Install the PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html),
connect the device over USB, then:

```bash
cd device
pio run -t upload                # M5StickC Plus2
pio run -e m5sticks3 -t upload   # M5StickS3
```

On first boot the device creates a Wi-Fi access point — connect to it and give
it your 2.4 GHz Wi-Fi credentials via the captive portal.

## Try it

1. Deploy the [`server/`](./server/) worker and set your `OPENAI_API_KEY`
   (see its README), then set `SERVER_HOST` in `device/src/main.cpp` to your
   worker host and flash.
2. The device prints its id and viewer URL to serial on connect.
3. Open `https://<your-worker-host>/devices/<deviceId>/`, **hold the front
   button**, and speak.
4. The FFT strip moves along the bottom and your words appear as a live
   transcript above. Release to stop.

A quick tap (under the hold threshold) is ignored — only a deliberate hold
starts streaming.

## How it works

The firmware uses `Resident::Sandbox` for driver wiring and the WebSocket
transport. The device connects to the Resident relay at `/devices/<id>` and
streams audio as binary frames on that same socket.

Push-to-talk is built entirely on Resident's core primitives, wired up in
`onSystemButtonHold` (see `device/src/main.cpp`):

- **Hold detection** — `PushButtonsDriver` doubles as the `SystemButton` role;
  the sandbox derives the hold/release gesture from it and calls back with
  `held`.
- **"Listening" overlay** — while held, `requestOverlay` raises a claim on
  the dual-role display and the overlay paints "Listening" in `onAcquire`.
  Because the display is dual-role, the claim suspends the app (there is no
  app here, so this just takes over the idle screen). On release the arbiter
  calls the display's `restoreContent()`, which repaints the idle prompt.
- **Mic streaming pump** — `startMicStream()` / `stopMicStream()` drive the
  sandbox's built-in pump, which drains `SystemMic` each `loop()` and ships
  16 kHz int16 PCM frames straight to the WebSocket.

There is no hand-rolled audio ring, long-press timer, or serial telemetry in
this example any more — all of that lives in `Resident::Sandbox` now.
