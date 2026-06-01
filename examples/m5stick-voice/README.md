# M5Stick Voice (push-to-talk)

A push-to-talk audio example for the M5StickC Plus2 / M5StickS3, built on the
[Resident](https://github.com/inanimate-tech/resident) sandbox and its drivers.
**Hold the front button to stream the microphone over a WebSocket.**

> **Milestone 1.** This proves the audio-streaming path with live transcription.
> The device streams 16 kHz PCM to the m5stick-voice server (a Cloudflare Worker)
> which forwards audio to OpenAI Realtime and streams the transcript back to a
> browser viewer. There is no Lua-app surface yet, so this example ships no
> `DEVICE-SKILL.md`.

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

A quick tap (under ~200ms) is ignored — only a deliberate hold starts streaming.

## Watching the stream (serial)

The firmware prints a once-per-second `[voice]` telemetry line while streaming —
handy for confirming a clean stream on new hardware or a new backend:

```
[voice] 76222 stat rec=34 sent=34 fail=0 micEn=1 connected=1 maxGap=32ms maxLoop=33ms
```

- `rec` / `sent` — frames captured vs. queued to the socket (should match)
- `fail` — `sendBinary` rejections (should stay 0)
- `micEn` / `connected` — mic enabled / WebSocket connected
- `maxGap` — worst gap between queued frames this second (~32ms = real-time 16 kHz)
- `maxLoop` — worst `loop()` duration this second; a spike here means a stall
  that bunches frames into a burst and shows up as stutter at the viewer

`pio device monitor` (or `pio run -t monitor`) to watch it.

## How it works

The firmware uses `Resident::Sandbox` for driver wiring and the WebSocket
transport. The device connects to the Resident relay at `/devices/<id>` and
streams audio as binary frames on that same socket. The button driver's
`setLongPress` callback provides push-to-talk: it fires once the hold passes a
200ms threshold and again on release. While held, `loop()` records 512-sample
frames and sends each as a binary WS frame.
