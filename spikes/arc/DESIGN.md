# arc — a reactive framework for generative device UI

**Status: spike.** Design + toy implementation. Nothing here is API-stable.

arc is a framework for writing Resident apps where the app is not the product
— the app is *the current output of an agent*. The agent generates behavior;
the device executes it inside the human interaction loop; the device streams
semantic events back; the agent revises. Apps must be equally writable by
humans and by models, because over the life of a device they will be written
by both, often interleaved.

The name is from the reflex arc: when you touch a hot stove, your spinal cord
withdraws your hand before the signal reaches your brain. The brain finds out
afterwards, and decides what to do *next*. That is this architecture. The
device runs reflexes; the agent is the brain; the wire between them is slow
and that's fine, because the reflexes were shipped ahead of time.

---

## 1. Premise, and why the ideal case fails

The ideal generative UI: every pixel, every buzzer tone, every fan-speed
value is produced by inference on the fly, and every touch event and IMU
sample streams back as tokens. The agent sits in a loop consuming raw input
and emitting raw output. A device is a pure I/O surface for a model.

This fails for two distinct reasons, and each failure points at a design
principle:

**Failure 1 — abstraction.** Humans and agents reason about *a button was
tapped*, not *touch-down at (48,112), touch-up at (49,110), 80ms apart*. Raw
I/O streams are the wrong altitude in both directions: pixel-pushing down,
sample-streaming up. The fix is the one UI programming already knows — 
components and events (React), streams with semantic operators (Rx), or on
embedded, a widget library like LVGL. **Principle: the wire should carry
meaning, not samples — in both directions.**

**Failure 2 — latency.** The screen is one network hop, one context-gather,
and one inference away from the model. Human interaction psychology needs
~150ms for "instant". Inference is 1–10s on a good day. No amount of
model speed fully closes this, because context-gathering (what's in the
room? what did the user do this morning?) is itself slow. **Principle: the
agent must ship behavior forward in time — code that covers the interaction
envelope for the next seconds-to-hours — rather than answering each input.**

The second principle is Cloudflare's codemode insight applied to devices:
the agent loop shouldn't be tool-call → inference → tool-call; the agent
writes small programs that collapse many steps into one artifact. Resident
is already codemode for devices — Lua source pushed over the network *is*
the collapsed program. arc is the question: what should those programs look
like, so that generating, reading, and revising them is easy?

A useful compression of the whole idea: **a program is cached inference.**
The agent doesn't generate outputs; it generates the *function from inputs
to outputs*, and the device evaluates that function at reflex speed.
Regeneration is cache invalidation.

## 2. The three loops

Everything in arc is organized around three nested loops, distinguished by
latency and by where the computation runs:

| Loop | Latency | Runs | Mechanism |
|------|---------|------|-----------|
| **Reflex** | < 150 ms | on device | shipped handlers, recognizers, view function |
| **Response** | 1–10 s | agent, per interaction | `ask` / reply — a hole in the shipped behavior, with a local fallback |
| **Revision** | minutes–hours | agent, per regime | regenerate the program; hydrate state across the swap |

The reflex loop is everything the shipped code can decide alone: toggle the
stopwatch, play the click, move the cursor, animate the dots. The response
loop is for interactions that *require* inference — the next sentence of the
story, the answer to a question — and the framework's job is to make the
seam ergonomic: instant local acknowledgment ("Thinking…"), then the reply
arrives as data. The revision loop is the agent changing the policy itself:
new activity, new mode, new UI — triggered by intent patterns (user seems
bored), schedules (morning vs evening), or external context.

A key property: **the agent participates at every loop, but only writes code
at the outermost one.** In the response loop it sends data (a reply). In the
reflex loop it isn't present at all — it was present earlier, when it wrote
the handler.

## 3. Core concepts

### 3.1 Intents — one bus, both directions

An **intent** is a named, semantic event: `toggle`, `choose{choice="a"}`,
`shake`, `wake`. All behavior is intent handlers. Intents come from three
sources and the handler cannot tell which:

- **Recognizers** — on-device compressors from raw input to intents
  (`arc.tap(0, "toggle")`, `arc.shake(2.0, "shake")`, `arc.idle(30000, "bored")`).
- **Local code** — `arc.intent("reset")` from a timer or another handler.
- **The agent** — any non-`arc:*` event arriving on the app channel is
  injected as an intent.

That last one matters: the agent is just another input device. It nudges the
running UI at the same abstraction level as the user's thumb. A reminder,
a mode switch, a "look alive" — all just intents, handled by shipped code.

Every intent is also published upstream (rate-limit-respecting, see §6).
This is the "token stream of inputs" from the premise, at the right
altitude: the agent sees `choose{choice="a"}`, not accelerometer samples.

### 3.2 Recognizers — the agent's sensory attention, as code

Recognizers are the up-direction abstraction: parameterized, named
compressors from the raw driver stream to intents. They are the Rx operator
library (debounce, threshold, window) but *packaged and named*, because
operator soup is exactly what humans misread and models miscompose.

The generative twist: since the agent writes the program, **the agent
chooses its own sensory bandwidth**. A sleep-tracking generation ships
sensitive motion recognizers and publishes everything; a stopwatch ships
button taps and publishes nearly nothing. Attention is code, revised per
regime. (In the limit a recognizer could be a distilled model — wake-word
style — running on-device. Same interface.)

### 3.3 The store — one table, and it outlives the code

All app state lives in a single store table (`s = arc.store{...}`). No
component-local state, no closures-as-state. This is deliberately Elm, not
React, and the reason is legibility: **the whole state of the device is one
small serializable value.** That buys three things:

1. The view can be a pure function of it.
2. The device can publish it (the *mirror*, §3.6).
3. It can survive regeneration (*hydration*): snapshot travels up; the
   agent bakes current values into the next generation's `arc.store{...}`
   defaults (or follows a load with `arc:hydrate`). The code is disposable;
   the store is continuous. State belongs to the *session*, not to the
   program — programs are cache entries.

### 3.4 The view — data, not drawing

`arc.view(fn)` registers a pure function returning a widget *tree of plain
tables*: `ui.col{ ui.label{text="12:04", size=4}, ui.bar{value=0.3} }`.
A dumb renderer lays it out and draws it via `screen.*` every tick
(10 FPS full redraw — reconciliation is pointless when a full frame on a
240×135 sprite is cheap; immediate-mode is simpler *and* more legible).

View-as-data is what makes the UI generative, because it makes the UI
*readable by the agent*: a framebuffer is illegible to a model at any
reasonable token budget, but `{col: [label "PAUSED", label "00:34.2"]}` is a
handful of tokens. The screen contents can literally appear in the agent's
context. It also makes the render backend swappable: the same tree could
target LVGL, an e-ink panel, or the three screens of a face (§9).

Outputs generally come in three kinds, and the view is only one:

- **View** — structured, discrete, redrawn: the screen tree(s).
- **Bindings** — continuous scalars sampled every tick: fan speed, LED
  brightness (`arc.every(100, function() fan.set(spd()) end)`).
- **Effects** — one-shot, fired from handlers: `buzzer.beep`, `arc.play{...}`.

### 3.5 Asks — Suspense for inference

The flagship primitive. Inside any intent handler:

```lua
local r = arc.ask("turn", { choice = p.choice }, {
  timeout_ms = 8000,
  fallback   = { text = "The mist thickens..." },
})
s.text = r.text
```

`ask` publishes the question upstream, suspends the handler (it's a
coroutine), and lets the reflex loop keep running — the view re-renders
immediately, and `arc.pending("turn")` is true so the view can show its
"Thinking…" state *within the 150ms budget*. When the reply event arrives
(or the timeout fires), the handler resumes with the reply (or the
fallback) and continues as if the call had been synchronous.

This is React Suspense with the roles shifted: the hole is not "data still
loading" but "decision not yet made", and the fallback is not a spinner
component but whatever the generation chose to ship — microcopy, a rustle
of leaves from the buzzer, a dimmed screen. Fallbacks are not optional
decoration; the network *will* be slow sometimes, so the language makes
the degraded path a required, named part of the construct.

**System chrome: inference is never silent.** Whenever any ask is in
flight the runtime itself draws a small pulsing indicator (three amber
dots, bottom-right), regardless of what the view does — like a browser's
loading spinner, it belongs to the platform, not the page. A generation
can layer richer pending treatments on top (and the view-writing prompt
encourages authored microcopy), but hiding agent activity requires an
explicit opt-out (`arc.activity(false)`). The default matters: if silently
running inference is the easiest thing to write, generated apps will do
it, and the user loses the thread of what the device is doing.

**The pending ask carries its payload.** `arc.pending(name)` returns the
in-flight ask's payload table (truthy), not a bare boolean — so the view
can *confirm the question* during the wait: highlight the option the user
just chose, echo the query, dim the road not taken. This is optimistic-UI
thinking adapted to the ask boundary: the local loop already knows what
was asked, and a pause that confirms the choice reassures where a generic
spinner merely stalls. (This came directly out of live use: "thinking" 
alone doesn't tell you *what it heard*.)

### 3.6 The mirror — the device as tokens

The agent can only generate good behavior if it can see the device. arc
maintains a compact snapshot — store contents, pending asks, generation id
— published on init, on `arc:probe`, and (throttled) after store changes.
Combined with the intent transcript and the current program source, the
agent's context contains: *what the device can do* (device profile), *what
it is currently doing* (the program — which the agent itself wrote, so
reading it is re-reading its own plan), *what state it's in* (mirror), and
*what the human has done* (intents). That is the entire loop made legible.

### 3.7 Generations — programs as disposable policy

A **generation** is one shipped program (Resident already names this:
`generationId`). arc treats generations as cheap and disposable: the agent
regenerates the whole L3 program (it's a few hundred tokens) rather than
patching code. Data-level patches flow through the running generation
(replies, hydrate, injected intents); behavior-level changes are a new
generation. Every ask carries the generation id so late replies to a dead
generation can be dropped (§6).

### 3.8 Timelines

Behavior spans time, so time is a primitive, not a library:
`arc.after(ms, fn)`, `arc.every(ms, fn)`, `arc.tween{from,to,ms,ease}`,
`arc.play{{freq,ms},...}`. Under the hood all of it is one scheduler on the
tick. This is also what makes hours-scale generations writable: a morning
briefing app is mostly timeline.

## 4. Prior art, mapped

| Source | What arc takes | What arc changes |
|--------|----------------|------------------|
| Elm architecture | single store, Msg≈intent, pure view | adds the oracle (`ask`) and cross-generation hydration |
| React | declarative view, components-as-vocabulary | immediate-mode, no reconciliation, no component-local state |
| Suspense | render-while-pending with fallback | the pending thing is *inference*, fallback is authored per-ask |
| Rx / FRP | recognizers are operators (debounce, threshold, scan) | named & parameterized, not composed inline |
| LVGL | the widget-vocabulary idea | widgets are data, not retained C objects; LVGL could be a *backend* |
| Cloudflare codemode | agent ships programs, not calls | the program's *shape* is constrained so it stays legible/revisable |
| Erlang hot swap | code replaced under a live system | state continuity via mirror+hydrate instead of process mailboxes |

## 5. Alternatives considered

**A. FRP-first (Rx everywhere).** Streams and operator composition as the
whole language. Rejected: operator chains are the least readable artifact
for both audiences (humans misread them; models miscompose them), and
per-event allocation churn is hostile to a microcontroller GC. The operator
*ideas* survive as recognizers.

**B. Mini-React (components, local state, reconciliation).** Rejected:
reconciliation buys nothing at 240×135@10FPS, and scattering state into
components destroys the mirror and hydration — the two properties the
agent loop depends on most.

**C. Server-driven UI (ship JSON views + bindings, no code).** The safe,
boring version: agent sends a declarative screen, device renders it.
Rejected as the *foundation* because reflex logic needs real code (SDUI
systems always sprout ad-hoc expression languages), but note that SDUI is
the degenerate case of arc — a generation whose handlers are trivial. arc
subsumes it.

**D. A new textual DSL.** Rejected: agents already speak Lua fluently; the
device already compiles Lua; the validator toolchain exists; and a compiled
DSL breaks "the agent reads the running policy" unless it round-trips.
arc is an embedded DSL — Lua tables and functions with strong conventions.

**E. LVGL as the widget layer.** Deferred: it's the right thing for visual
polish on bigger screens, but its imperative retained-object API is a bad
*generative target* (illegible as data, stateful to drive from a sandbox).
The right integration is LVGL as a render backend for the arc tree.

## 6. Runtime architecture

arc is layered; each layer has one owner:

```
L3  the generation        — written by the agent (or a human), per regime
L2  device profile        — widget set + recognizers for this hardware (per DEVICE-SKILL)
L1  arc runtime (arc.lua) — pure Lua, ~500 lines, same for every device
L0  Resident sandbox      — C++ firmware (exists today, unchanged)
```

The server concatenates L1 (+L2) with the generation and pushes one `app`
message. The agent only ever writes L3 — the runtime costs no tokens in the
agent's output, only bytes on the wire.

**Mapping to the sandbox.** arc.lua defines the three lifecycle globals
itself; generations never do:

- `init` — render first frame, publish hello snapshot.
- `on_tick` — scheduler (timers, tweens, tones) → recognizer polling (IMU,
  idle) → ask-timeout sweep → outbox drain → render.
- `on_event` — `arc:*` types handled internally (reply/hydrate/probe);
  `button` events feed tap recognizers; anything else is injected as an
  intent. After dispatch, if the store changed, render immediately —
  event delivery runs at main-loop rate, so input→pixel is well under the
  150ms budget even though the tick is 100ms.

**Wire protocol.** Everything rides the existing channel plane; no firmware
changes needed.

Up (data plane, `events.send` — flat tables, ≤256 bytes, 5/s bucket):

| name | payload | when |
|------|---------|------|
| `arc:intent` | `{name, ...payload}` | every intent (through the outbox) |
| `arc:ask` | `{ask, id, gen, ...payload}` | on `arc.ask` |
| `arc:state` | `{gen, ...flattened store}` | init, probe, throttled after store changes |

Down (app channel messages; `event.name` = type):

| type | payload | effect |
|------|---------|--------|
| `arc:reply` | `{id, ...data}` | resume the suspended handler |
| `arc:hydrate` | `{...}` | merge keys into the store |
| `arc:probe` | `{}` | publish `arc:state` |
| *anything else* | `{...}` | injected as an intent |

**Budgets are a feature.** The 256-byte/5-per-second data plane forces
exactly the semantic compression §1 argues for. The outbox retries on
rate-limit (`events.send` returning false), drops oldest intents under
pressure, never drops asks. Payloads that genuinely need more (audio, big
state) belong on the system channel or the mic pump — out of scope here.

**The generation race.** An ask can be in flight when a new generation
loads; the reply would then arrive at a program that never asked it. Every
ask carries `gen` (the server bakes `arc._gen = "<id>"` into the concat;
core could expose `ctx.generation_id` later and remove the hack). The
server drops replies addressed to a dead generation — and since the
snapshot travels with the ask, it can usually answer the ask *into the new
generation* instead (bake the answer into the regenerated code).

## 7. The language, by example

The complete stopwatch (reflex loop only — this runs with zero agent
involvement after shipping):

```lua
local s = arc.store{ running = false, elapsed = 0 }

arc.tap(0, "toggle")
arc.tap(1, "reset")

arc.on("toggle", function()
  s.running = not s.running
  buzzer.beep(s.running and 880 or 440, 40)
end)

arc.on("reset", function()
  s.running = false; s.elapsed = 0
  arc.play{ {660,40}, {0,30}, {440,60} }
end)

arc.every(100, function(dt)
  if s.running then s.elapsed = s.elapsed + dt end
end)

arc.view(function()
  return ui.col{ pad = 8, gap = 6, align = "center",
    ui.label{ text = arc.fmt_ms(s.elapsed), size = 4,
              color = s.running and "green" or "white" },
    ui.label{ text = s.running and "RUNNING" or "PAUSED",
              size = 1, color = "gray" },
    ui.label{ text = "A start/stop   B reset", size = 1, color = "dim" },
  }
end)
```

And the response loop, from the choose-your-own-adventure generation:

```lua
arc.on("choose", function(p)
  buzzer.beep(660, 30)                      -- reflex: instant acknowledgment
  local r = arc.ask("turn", { choice = p.choice }, {
    timeout_ms = 9000,
    fallback = { text = "The path is dark. Choose again.",
                 a = s.a, b = s.b },
  })
  s.text, s.a, s.b = r.text, r.a or s.a, r.b or s.b
end)

arc.view(function()
  return ui.col{ pad = 6, gap = 4,
    ui.text{ text = s.text, size = 2, w = 228 },   -- word-wrapped
    arc.pending("turn")
      and ui.label{ text = "thinking" .. string.rep(".", arc.dots(3)),
                    color = "amber" }
      or  ui.col{ gap = 2,
            ui.label{ text = "A: " .. s.a, size = 1, color = "cyan" },
            ui.label{ text = "B: " .. s.b, size = 1, color = "cyan" } },
  }
end)
```

The full API surface is small enough to list (and small enough to sit in a
system prompt, which is the point):

```
arc.store{defaults}                      one per app; returns the store
arc.on(name, fn)                         intent handler (coroutine; gets payload)
arc.tap(idx, name)                       button tap → intent
arc.shake(g, name)                       accel magnitude → intent (1s refractory)
arc.idle(ms, name)                       quiet spell → intent (once per spell)
arc.intent(name, payload?)               inject an intent locally
arc.ask(name, payload?, {timeout_ms, fallback})   suspend on the agent (handlers only)
arc.pending(name?)                       ask in flight?
arc.every(ms, fn) / arc.after(ms, fn)    timers → handle; arc.cancel(handle)
arc.tween{from,to,ms,ease}               returns getter fn
arc.play{{freq,ms},...}                  tone sequence (0 = rest)
arc.view(fn)                             fn() → widget tree
arc.publish(name, payload)               raw upstream event (rare)
arc.fmt_ms(ms) / arc.dots(n)             tiny conveniences

ui.col{pad,gap,align,...} ui.row{...}    layout (align: "start"|"center")
ui.label{text,size,color}                one line
ui.text{text,size,color,w}               word-wrapped block
ui.rect{w,h,color,fill} ui.bar{value,w,h,color}
ui.space{w,h} ui.qr{data,scale} ui.canvas{w,h,draw}
colors: "white" "black" "gray" "dim" "red" "green" "blue" "amber" "cyan" "magenta" or {r,g,b}
```

Conventions that keep generations legible (enforced socially / by prompt,
not by the runtime): one store, declared first; recognizers next; handlers
next; timelines; view last. A generation should fit in ~60 lines. If it
doesn't, the regime is probably two regimes.

## 8. The agent loop

Server-side (a `DeviceAgent` subclass in the existing server-template — 
sketch, not shipped in this spike):

```ts
class ArcAgent extends DeviceAgent {
  // durable state: deviceProfile (DEVICE-SKILL.md), generation source,
  // mirror (latest arc:state), intent transcript (ring)
  async onMessage(conn, msg) {
    const e = parse(msg)
    if (e.name === "arc:ask")    return this.answerAsk(e)      // response loop
    if (e.name === "arc:state")  return this.mirror.update(e)
    if (e.name === "arc:intent") {
      this.transcript.push(e)
      if (this.regimeChanged(e))  return this.regenerate()      // revision loop
    }
  }
  async answerAsk(e) {
    const reply = await claude({                                // claude-fable-5 /
      system: ARC_CHEATSHEET + this.deviceProfile,              // claude-haiku for cheap asks
      messages: context(this.generation, this.mirror, this.transcript, e),
    })
    this.send({ channel: "app", type: "arc:reply", data: { id: e.id, ...reply } })
  }
  async regenerate() {
    const l3 = await claude({ /* profile + previous generation + mirror +
      transcript + brief; asks for a NEW ~60-line arc generation */ })
    this.send({ channel: "system", type: "app", code: ARC_LUA + gen(l3) })
    this.send({ channel: "app", type: "arc:hydrate", data: carryOver(this.mirror) })
  }
}
```

Notes that matter more than the sketch:

- **The previous generation is chain-of-thought.** When regenerating, the
  agent reads its own last program — the policy is the plan, in a form that
  diffs. This is why legibility conventions (§7) are load-bearing and not
  aesthetics.
- **Two model tiers.** Asks are latency-sensitive and narrow → small fast
  model with a tight cheatsheet. Regenerations are rare and broad → big
  model. The fallback covers both being slow.
- **When to regenerate:** explicit user intent ("bored", a mode gesture),
  schedule (`arc.idle` firing at night → ship the night generation),
  ask-pressure (if every interaction escalates, the policy is at the wrong
  altitude — push more behavior down), or external context changing.
- **Fully generative, bootstrapped:** device connects → server has no
  generation → ships a stock "hello" generation whose only behavior is
  publishing intents and asking `"what should I be?"` — from there the
  loop is closed and everything on the screen originated in inference,
  cached as code.

## 9. Honest limits & open problems

Found by doing the design; kept here so the spike is a fair record.

1. **Hydration is a round-trip.** State survives regeneration only via the
   server. Device-side continuity (a store slot that survives `loadApp`,
   deliberately exempt from `onAppReset`) would be better — candidate core
   feature, with real design questions (when *should* it clear?).
2. **Screen blanks on regeneration.** `loadApp` clears the sprite; a swap
   visibly blinks. Mitigations: regenerate during natural blackouts, or a
   core option to preserve the framebuffer until the first `flip`.
3. **Recognizer poverty upstream.** The m5stick driver emits only
   button-press events — no down/up, so no hold/long-press recognizers.
   arc defines the raw vocabulary drivers *should* emit (down/up/repeat,
   touch points, dial deltas); that's feedback into driver design.
4. **10 FPS render quantum.** Fine for text/status UI; choppy for motion.
   The shader path stays the high-FPS escape hatch; a core tick-rate config
   is plausible.
5. **256B flat payloads.** Right-sized for intents; tight for asks with
   context and for the mirror of a bigger app. Chunking or a system-channel
   sidecar would be needed past toy scale.
6. **Event ring is 8 deep**, one delivery per loop — a fast tapper during a
   pending ask could drop events. Acceptable at toy scale; worth a counter.
7. **No partial code patching.** Whole-generation replacement is the only
   behavior update. That's a considered position (§3.7), but component-level
   regeneration (patch one handler) may earn its complexity once
   generations grow. Requires `load()` in-sandbox or core support.
8. **Multi-surface is designed, not built.** For the fan/face: the view
   returns named surface trees (`{main=..., left=...}`) and continuous
   actuators are bindings (§3.4). The toy targets the single m5stick screen.
9. **`arc._gen` is a bake-in hack** — core should expose the generation id
   to Lua (`ctx.generation_id`).
10. **Runtime weight.** L1+L3 must fit compile-time and NVS persistence
    budgets (`persist_too_big`). arc.lua stays under ~15KB; if it grows,
    it moves into firmware as a preloaded module (which also fixes 2 and 7).

Found live (examples/m5stick-arc, the deployed demo):

11. **`events.send` doesn't escape JSON strings.** A quote in a value
    corrupts the data payload, which then arrives as `{}`. arc sanitizes
    (`"`→`'`, `\`→`/`) before publishing; the real fix is escaping in the
    C++ serializer.
12. **workers.dev TLS is Let's Encrypt**, not Cloudflare universal SSL, so
    Courier's embedded GTS fallback root fails there when the build lacks
    the IDF cert bundle (Arduino builds do). Devices pointed at workers.dev
    must pin the ISRG roots.
13. **A worker deploy resets the Durable Object** and drops the device's
    WebSocket; sessions resume on reconnect but in-flight asks die (the
    shipped fallback covers it — the design earning its keep).
14. **Model-written views work.** claude-sonnet-5 reliably produces valid
    arc view chunks under a ~2200-char budget with a luaparse syntax gate
    and one retry-with-feedback; the renderer's pcall makes the residual
    risk cosmetic rather than fatal.

## 10. The toy, and how to run it

```
spikes/arc/
  DESIGN.md          this file
  arc.lua            L1 runtime (pure Lua, no host deps)
  apps/stopwatch.lua reflex-only generation
  apps/adventure.lua response-loop generation (ask/reply/fallback)
  sim/sim.lua        host-side harness: stub drivers, tick clock, event injection
  sim/scenarios.lua  scripted runs + assertions (the sim's test suite)
```

Run the scenarios (any Lua ≥5.1 on the host):

```bash
cd spikes/arc && lua sim/scenarios.lua
```

Push a generation to a real m5stick: concatenate runtime + generation and
send it as a normal app —

```bash
cat arc.lua apps/stopwatch.lua > /tmp/out.lua
# then push /tmp/out.lua with the usual resident:push-app flow
```

The sim stubs the m5stick surface from DEVICE-SKILL.md (`screen`, `imu`,
`buzzer`, `button`, `events`, `log`), drives `on_tick` with a fake clock,
injects `button`/`arc:*` events, and captures `events.send` — enough to
exercise the full ask round-trip, timeout fallback, hydration, and the
outbox's rate-limit retry without hardware.

## 11. Roadmap sketch

1. **Now (this spike):** concepts, L1 toy, two generations, sim.
2. **Next:** ship arc.lua via a real server (`ArcAgent` on server-template),
   wire a live m5stick, measure the seams (ask latency distribution, blink
   on regen, rate-limit pressure).
3. **Then:** device profiles for a second device (the fan — bindings,
   no screen) to pressure-test the output taxonomy; store-survives-reload
   in core; richer raw event vocabulary in drivers.
4. **Later:** LVGL backend behind the same tree; component-level
   regeneration; recognizers-as-models.
