# m5stick-voice server

A Cloudflare Worker that turns the m5stick-voice device's push-to-talk audio
into a **live transcript**. It subclasses the Resident relay
(`DeviceAgent` from [`@inanimate/resident`](https://www.npmjs.com/package/@inanimate/resident)),
so the device uses one WebSocket for both Lua-app push and binary audio.

```
device ──/devices/<id>──► VoiceAgent ──► OpenAI Realtime (transcription)
                              │                    │
                  binary audio│         transcript │ delta/completed
                              ▼                    ▼
                          monitors (browser): FFT strip + live transcript
```

## Deploy

```bash
cd examples/m5stick-voice/server
npm install
npx wrangler deploy
```

Set your OpenAI key as a secret (never commit it):

```bash
npx wrangler secret put OPENAI_API_KEY
# paste your sk-... key when prompted
```

For local dev, put the key in `examples/m5stick-voice/server/.dev.vars`
(git-ignored) instead:

```
OPENAI_API_KEY=sk-...
```
then `npm run dev`.

## Point the device at it

In `../device/src/main.cpp`, set `SERVER_HOST` to your worker's host (replace
the `YOUR-CF-ACCOUNT` placeholder), then `pio run -t upload`. The device prints
its id and the viewer URL to serial on connect.

## Watch

Open `https://<your-worker-host>/devices/<deviceId>/` in a browser, hold the
device's front button, and speak — the FFT strip moves along the bottom and the
transcript fills in above.

## Notes

- Transcription uses the OpenAI Realtime API (`gpt-realtime-whisper`), configured
  for 16 kHz PCM to match the device. If OpenAI rejects 16 kHz, switch the
  session `format.rate` to `24000` and upsample in `appendAudio`.
- The relay is unauthenticated beyond the device id (same caveat as
  `examples/m5stick-demo`) — fine for hacking, not for production.
- No `OPENAI_API_KEY`? The FFT still works; the transcript just stays empty and
  the worker logs the missing key.
