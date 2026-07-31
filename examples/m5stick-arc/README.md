# m5stick-arc

A live demo of **arc** (see `spikes/arc/DESIGN.md`), the reactive framework
for generative device UI, on an M5Stick: a choose-your-own-adventure whose
next scene is written by a model, on the fly, through Cloudflare AI Gateway.

The three loops, live:

- **Reflex** (on the stick, <150ms): button press → beep + waiting
  indicator, instantly, from the shipped Lua generation. Tilting the stick
  sways the lantern glow at 10 FPS with zero network traffic — but the tilt
  value rides along in the next ask, so how you hold the lantern quietly
  shapes the story.
- **Response** (1–10s): the press escalates as an `arc:ask` event; the
  server answers it with a fast model (`claude-haiku-4-5`) through AI
  Gateway and the scene updates on the reply. A 9s timeout falls back to
  shipped microcopy.
- **Revision** (the generative-UI part): **shake the stick** (or the
  viewer's Remix button) and a stronger model (`claude-sonnet-5`) writes a
  brand-new Lua *view function* — layout, palette, decoration, motion, and
  its own waiting-state microcopy. The server syntax-checks it (luaparse +
  a banned-construct vet), splices it into the generation, pushes it to the
  device, and re-hydrates the story state so play continues where it left
  off. Every shake produces a different UI, authored by a model at runtime.
  A rejected or failed remix degrades to an `arc:reply` and the current
  look survives; a view that errors at runtime is caught by the arc
  renderer's pcall and logged, never crashing the app.

## Layout

```
app/arc.lua        the arc L1 runtime (copy of spikes/arc/arc.lua)
app/adventure.lua  the generation (copy of spikes/arc/apps/adventure.lua)
device/            PlatformIO firmware (same drivers as m5stick-demo)
server/            Cloudflare Worker: ArcAgent relay + inference + viewer
```

## Server

```bash
cd server
npm install
CLOUDFLARE_ACCOUNT_ID=<your account id> npx wrangler deploy
```

The worker uses the AI Gateway named by the `CLOUDFLARE_AIG_ID` var (BYOK:
the gateway's secrets store holds the Anthropic key, the worker holds no
model secret). Asks are answered by a fast model (`claude-haiku-4-5`);
replies are budgeted to the device's 256-byte event buffer.

## Device

Build with the deployed hostname (or edit `RESIDENT_HOST` in
`device/src/main.cpp`):

```bash
cd device
PLATFORMIO_BUILD_FLAGS='-DARC_HOST=\"<host>\"' \
  pio run -e m5sticks3 -t upload   # or -e m5stick for the M5StickC Plus2
```

Note `src/le_roots.h`: workers.dev serves a Let's Encrypt chain, but this
Arduino build has no IDF cert bundle and Courier's embedded fallback root
(GTS Root R4) only covers Cloudflare universal SSL — so `main.cpp` pins the
ISRG roots via `onConfigureNetwork`. Skip this if you front the worker with
a custom domain on Cloudflare.

## Run it

Open `https://<host>/devices/<deviceId>` in a browser — the live viewer
shows the device mirror (`arc:state`), the full message stream in both
directions, and per-ask latency. Buttons:

- **Push adventure app** — ship a fresh generation (default look, story
  reset) to the stick.
- **Choose A / B / Remix** — inject the same intents the physical buttons
  and the shake gesture produce (in arc, the agent is just another input
  device; the handlers can't tell).
- **Probe** — ask the device to publish its mirror.

Or from the stick itself: held landscape, the **top-edge side button**
picks choice A (the top option) and the **front face button** picks B;
**shake** remixes the look; two minutes of quiet publishes a `bored`
intent (visible in the viewer — a fuller agent would respond by
regenerating the app into something else entirely). Whenever the agent is
working — a turn or a remix — the arc runtime itself pulses three amber
dots in the bottom-right corner, on top of whatever waiting state the
current view has authored; that's framework chrome, not app code.

## Voice (push-to-talk)

**Hold the front button and speak; release to send.** The driver's
long-press detector doubles as tap-suppression, so holding to talk never
registers as a choice-B tap. While held, the firmware brackets the mic
pump's raw 16 kHz PCM with `voice start/end` system-channel envelopes and
injects `ptt` events so the running app can show its listening state.

Server-side, the take is transcribed with Workers AI whisper
(`@cf/openai/whisper-large-v3-turbo`, via the AI Gateway binding — still
no secrets in the worker), and the transcript is immediately sent back as
a `heard` event — the app shows what the device heard before anything is
done about it.

Then a model decides what the words *mean* — nothing is hardcoded. The
router is given the device's current Lua program, its live store, and the
utterance, and returns one of:

- **intent** — "I should take that lantern" ≈ choice A → it injects
  `choose_a` onto the normal bus, indistinguishable from a button press;
- **turn** — "I want to climb the tree and look at the moon" fits no
  intent → the narrator weaves it into the next beat, delivered as an
  `arc:hydrate`;
- **ignore** — noise.

The generation needed zero framework changes for any of this: `ptt` and
`heard` are ordinary intents, and voice-routed actions reuse the existing
reply/hydrate paths.

Voice can be exercised without hardware: synthesize a take
(`say -o take.wav --file-format=WAVE --data-format=LEI16@16000 "..."`)
and stream it over a plain `/devices/<id>` WebSocket between `voice`
start/end envelopes, watching the result on the monitor socket.

`POST /devices/<id>/arc/remix` runs the same remix as the shake but returns
vet/inference diagnostics in the response — useful when prompt-tuning the
view writer.

Findings that fed back into arc/Resident from getting this live, kept in
`spikes/arc/DESIGN.md` §9: the device's `events.send` serializer does not
escape quotes (arc sanitizes strings before publishing); replies must fit
the 256-byte incoming event buffer (the server trims to fit, at word
boundaries); a worker deploy resets the Durable Object and drops the
device WebSocket mid-session.
