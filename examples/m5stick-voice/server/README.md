# m5stick-voice server

A Cloudflare Worker that turns the [m5stick-voice](../) device's push-to-talk
audio into a **live transcript in the browser**. Hold the device's button and
speak; your words appear on a web page, with a colourful audio visualiser along
the bottom.

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

### 3. (Optional) Route through Cloudflare AI Gateway

For analytics / caching / rate-limiting, send the OpenAI traffic through
[AI Gateway](https://developers.cloudflare.com/ai-gateway/). Create a gateway in
the dashboard (**AI → AI Gateway**), then set:

```bash
npx wrangler secret put CF_ACCOUNT_ID    # your Cloudflare account id
npx wrangler secret put AI_GATEWAY_ID    # the gateway name
```

When both are set the worker connects via the gateway; otherwise it goes direct.
If you set the gateway to **Authenticated**, also
`npx wrangler secret put CF_AIG_TOKEN` (a token with the *AI Gateway Run*
permission) — otherwise the gateway accepts the WebSocket handshake and then
drops it. To temporarily bypass the gateway without deleting the secrets, set
`OPENAI_DIRECT=1`.

### 4. Point the device at your worker

In [`../device/src/main.cpp`](../device/src/main.cpp), set `SERVER_HOST` to your
worker's host (replace the `YOUR-CF-ACCOUNT` placeholder), then flash:

```bash
cd ../device && pio run -t upload -t monitor
```

The device prints its id and the full viewer URL to serial on connect.

### 5. Watch

Open `https://<your-worker-host>/devices/<deviceId>/` in a browser, hold the
device's front button, and speak. The FFT strip moves along the bottom and the
transcript fills in above. (You can drop the trailing slash —
`…/devices/<deviceId>` works too.)

## How it works

- **One socket.** `VoiceAgent` overrides `onMessage`: binary frames (audio) are
  fanned to monitor connections (for the FFT) and pushed into the OpenAI bridge;
  everything else falls through to the canonical Resident relay.
- **OpenAI Realtime (GA API).** The bridge opens a WebSocket to
  `…/v1/realtime?intent=transcription` (no `OpenAI-Beta` header — that selects
  the retired Beta shape) and configures a transcription session with the
  `gpt-realtime-whisper` model.
- **16 kHz → 24 kHz.** The device captures 16 kHz PCM16, but the realtime
  transcription input requires ≥ 24 kHz, so the worker linearly upsamples each
  frame (3:2) before sending.
- **Commit on silence.** `gpt-realtime-whisper` has no server-side VAD, so the
  worker commits the audio buffer after ~0.7 s of no frames (≈ the button
  release), which transcribes the turn. Transcripts therefore appear
  **per utterance** (speak → release → text), not streaming word-by-word. For
  live word-by-word deltas instead, switch the model to `gpt-4o-transcribe` and
  add a `turn_detection: { type: "server_vad" }` block to the session config.
- **To the browser.** Transcript `delta`/`completed` events become JSON frames
  (`{type:"transcript.delta"|"transcript.completed", text, itemId}`) sent to the
  monitor connections; the viewer renders them above the FFT.

## Notes

- The relay is unauthenticated beyond the device id (same caveat as
  [`../../m5stick-demo`](../../m5stick-demo)) — fine for hacking, not for
  production.
- No `OPENAI_API_KEY` set? Audio still reaches the browser so the FFT animates;
  the transcript stays empty and the worker logs the missing key.
