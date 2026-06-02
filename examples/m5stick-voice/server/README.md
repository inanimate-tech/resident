# m5stick-voice server

A Cloudflare Worker that turns the [m5stick-voice](../) device's push-to-talk
audio into a **live transcript** *and* a **voice-controlled background**. Hold
the device's button and speak; your words appear on a web page, and the model
can repaint the page background by calling a CSS tool ("make it blue with
diagonal stripes, then animate them faster").

It's built by subclassing the Resident relay — `DeviceAgent` from
[`@inanimate/resident`](https://www.npmjs.com/package/@inanimate/resident) — so
the device uses **one** WebSocket for everything: the normal Resident protocol
(Lua-app push, events) *and* the binary audio frames. The worker bridges that
audio to the OpenAI Realtime transcription API and fans both the audio and the
transcript out to any browser "monitor" connections.

```
                     ┌──────────── VoiceAgent (one Durable Object per device) ───────────┐
device ──/devices/<id>──►  binary audio ──► resample 16k→24k ──► OpenAI Realtime (transcription)
   (Resident WS)        │        │                                          │
                        │        └──► monitors (FFT)        transcript ◄─────┘
                        │                                   delta/completed
                        └──────────────► monitors ◄─────────────────────────┘
                                              │  (JSON transcript frames)
                                              ▼
                       browser viewer at GET /devices/<id>/  — transcript + FFT strip
```

## What you need

- An **OpenAI API key on an account with billing enabled** (the realtime
  transcription model is paid; without credit you'll get `insufficient_quota`).
- A **Cloudflare account** (Workers + Durable Objects; the free plan is fine).
- The m5stick-voice device (see [`../device`](../device)).

## Setup

### 1. Install + deploy

```bash
cd examples/m5stick-voice/server
npm install
npx wrangler deploy        # note the worker host it prints, e.g. m5stick-voice.<acct>.workers.dev
```

### 2. Provide the OpenAI key

For the **deployed** worker, store it as a secret (never committed):

```bash
npx wrangler secret put OPENAI_API_KEY
# paste your sk-... key when prompted
```

For **local dev** (`npm run dev`), copy the example env file and fill it in:

```bash
cp .dev.vars.example .dev.vars   # .dev.vars is git-ignored
# edit .dev.vars and set OPENAI_API_KEY
npm run dev
```

`.dev.vars.example` lists every variable this worker reads.

> **Why not Cloudflare AI Gateway?** As of mid-2026 the AI Gateway realtime
> WebSocket proxy is built around `gpt-4o-realtime-preview` + the retired
> `OpenAI-Beta` header and drops connections that use the GA models this example
> needs (`gpt-realtime-2` / `gpt-realtime-whisper`). So the worker connects to
> OpenAI directly. (Normal HTTP requests through the gateway work fine — it's
> specific to the realtime WS proxy.)

### 3. Point the device at your worker

In [`../device/src/main.cpp`](../device/src/main.cpp), set `SERVER_HOST` to your
worker's host (replace the `YOUR-CF-ACCOUNT` placeholder), then flash:

```bash
cd ../device && pio run -t upload -t monitor
```

The device prints its id and the full viewer URL to serial on connect.

### 4. Watch

Open `https://<your-worker-host>/devices/<deviceId>/` in a browser, hold the
device's front button, and speak. The FFT strip moves along the bottom and the
transcript fills in above. (You can drop the trailing slash —
`…/devices/<deviceId>` works too.)

## How it works

- **One socket.** `VoiceAgent` overrides `onMessage`: binary frames (audio) are
  fanned to monitor connections (for the FFT) and pushed into the OpenAI bridge;
  everything else falls through to the canonical Resident relay.
- **OpenAI Realtime (GA API), conversational.** The bridge opens a WebSocket to
  `…/v1/realtime?model=gpt-realtime-2` (no `OpenAI-Beta` header — that selects
  the retired Beta shape). The session enables input transcription
  (`gpt-realtime-whisper`) **and** declares an `apply_css` function tool.
- **16 kHz → 24 kHz.** The device captures 16 kHz PCM16, but the realtime input
  requires ≥ 24 kHz, so the worker linearly upsamples each frame (3:2) before
  sending.
- **Push-to-talk turns.** `turn_detection` is `null`; ~0.7 s after the audio
  stops (button release) the worker commits the buffer and sends
  `response.create`. The commit drives the transcript; the response lets the
  model decide whether to call `apply_css`.
- **The CSS tool.** When the model calls `apply_css({css})`, the worker
  broadcasts a `{type:"css"}` frame to the browser, which replaces the page's
  `#agent-css` stylesheet (the model paints a full-viewport `#bg` layer). Then
  the worker returns the tool result and another `response.create` so a single
  spoken sentence can chain several changes.
- **To the browser.** Transcript `delta`/`completed` events become JSON frames
  (`{type:"transcript.delta"|"transcript.completed", text, itemId}`) sent to the
  monitor connections; the viewer renders them above the FFT.

## Notes

- The relay is unauthenticated beyond the device id (same caveat as
  [`../../m5stick-demo`](../../m5stick-demo)) — fine for hacking, not for
  production.
- No `OPENAI_API_KEY` set? Audio still reaches the browser so the FFT animates;
  the transcript stays empty and the worker logs the missing key.
- The session model is `gpt-realtime-2` (a constant `REALTIME_MODEL` in
  `worker.ts`) — swap it in one line. A conversational session with input
  transcription costs more than transcription-only.
- The injected background CSS is model-authored. CSS can't run JS, but can load
  external URLs and cover the page — fine for a local demo, not for exposing to
  untrusted speakers.
