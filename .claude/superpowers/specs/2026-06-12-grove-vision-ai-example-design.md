# Design: `examples/m5stick-grove-vision-ai` — Grove Vision AI V2 driver + example

Date: 2026-06-12 · Branch: `spike/grove` · Status: approved by Matt (pending spec review)

## Goal

A new Resident example pairing the M5StickS3 with the Seeed Grove Vision AI V2
module (Himax WiseEye2, I2C addr 0x62 over the Grove connector). A new driver
polls the module and feeds inference results into the sandbox as events plus a
pollable Lua module, so Lua apps can react to vision — model-agnostically,
because Matt will flash different SenseCraft models onto the module while
experimenting.

Reference implementation: `~/code/wave-demo-spike` (verified working
M5StickS3 + Vision AI V2 + YOLOv8-pose pipeline using
`Seeed_Arduino_SSCMA@^1.0`; Grove I2C SDA=9, SCL=10, ~6 Hz invoke rate, stock
SenseCraft firmware on the module).

## Key facts driving the design

- **SSCMA fixes the result surface regardless of model.** Every model returns
  one or more of four result types: `boxes` (x,y,w,h,score,target), `classes`
  (target,score), `points` (x,y,z,score,target), `keypoints` (box + N points;
  pose = 17 COCO keypoints/person). A model-agnostic driver needs no changes
  when models are swapped.
- **Models are flashed via the SenseCraft web studio** (USB to the module) —
  not switchable from the host at runtime.
- **Resident driver events serialize into 256-byte JSON** (8-slot ring, one
  event dispatched per `loop()` pass). A 17-keypoint pose frame cannot fit in
  one event → condensed events + pollable detail API.
- House precedent: examples symlink m5stick-demo's shared drivers
  (`symlink://../../m5stick-demo/device/lib/drivers`), new drivers live in the
  new example's own `lib/`.

## 1. Project layout (copied from m5stick-demo, S3-only)

```
examples/m5stick-grove-vision-ai/
├── README.md                  # hardware wiring, model flashing via SenseCraft, build
├── DEVICE-SKILL.md            # Lua surface for app authors (incl. validation stubs)
├── send-app.sh
├── device-apps/               # demo Lua apps (§5)
└── device/
    ├── platformio.ini         # [env:m5sticks3] only
    ├── partitions.csv
    ├── lib/vision/            # GroveVisionDriver (new code)
    └── src/main.cpp           # m5stick-demo main + vision driver registered
```

- `platformio.ini` lib_deps: `symlink://../../..` (resident),
  `symlink://../../m5stick-demo/device/lib/drivers` (shared M5Stick drivers),
  `symlink://lib/vision` (new driver), `seeed-studio/Seeed_Arduino_SSCMA@^1.0`,
  M5Unified.
- `deviceType = "m5stick-vision"` → WS path `/agents/m5stick-vision-agent/<id>`.
- `SERVER_HOST` stays a `YOUR-CF-ACCOUNT` placeholder (public repo rule).

## 2. GroveVisionDriver (`Resident::Driver`, in `lib/vision/`)

- **Config struct**: SDA pin (default 9), SCL pin (default 10), poll interval
  ms (default 200 ≈ 5 Hz), I2C clock, verbose logging flag (default ON).
- **`begin()`**: `Wire.begin(sda, scl)`; `ai.begin(&wire)`. On success, query
  and log `ai.name()` / `ai.info()` so serial identifies the flashed model.
  Module absent ≠ crash: mark link down, retry with backoff from `update()`
  (wave-demo "MODULE OFFLINE" lesson).
- **`update()`**: rate-limited `ai.invoke(1, false, false)`; copy SSCMA result
  vectors into a cached frame; classify kind with precedence
  `keypoints → boxes → points → classes` (→ `pose`/`boxes`/`points`/`classes`);
  emit condensed event (§3); verbose-log the frame (§6).
- **Link state machine**: consecutive invoke failures → link down (emit link
  event, log); successful invoke after down → link up (emit, log, re-query
  model name).

## 3. Events (driver `sendEvent`, name `"vision"`)

All fit the 256-byte budget for every model type:

- Frame with detections: `kind` (string), `n` (int), best detection flattened:
  `target`, `score`, `x`, `y`, `w`, `h` (box-ish kinds; `classes` omits box
  fields; `points` uses x,y,z). "Best" = highest score.
- Transition to empty: single event with `kind`, `n=0` (no per-frame spam
  while the scene stays empty).
- Link transitions: `kind="link"`, `ok=0|1`.

## 4. Lua module `vision` (registerModule on the driver)

Serves full detail from the cached last frame; apps poll during `on_tick`:

```lua
vision.kind()          -- "pose"|"boxes"|"classes"|"points"|"none"
vision.count()         -- detections in last frame
vision.detection(i)    -- 1-based; table with fields per kind:
                       --   boxes/pose: x,y,w,h,score,target
                       --   classes:    score,target
                       --   points:     x,y,z,score,target
vision.keypoint(i, k)  -- pose person i, COCO keypoint k (1..17): table x,y,score
vision.age_ms()        -- ms since last successful invoke (staleness check)
vision.ok()            -- link up?
```

Out-of-range indices return nil. `keypoint()` returns nil for non-pose kinds.

Units, everywhere (events, module, logs): coordinates are integer pixels in
the model's frame as SSCMA reports them (no normalization); scores are
integers 0–100. Values pass through untransformed so what Lua sees matches
what serial logging shows.

`target` is an integer class index in all result types; the label table
("rock"/"paper"/"scissors", "face", …) lives in model metadata returned by
`ai.info()`. The driver logs that at startup/link-up. Opportunistic extra,
not a commitment: if the metadata parses reliably on real firmware, expose
`vision.target_name(t)` → string|nil. Zoo ground truth for commonality:
gesture (RPS), face, person/apple/strawberry/pet/intrusion are all Swift-YOLO
detection models → `boxes`; person-classification/gender → `classes`;
YOLOv8 pose → `keypoints`.

## 5. Demo apps (`device-apps/`)

Keep the inherited m5stick-demo apps; add three vision apps that between them
exercise events, the poll API, and all result kinds:

- `presence.lua` — event-driven: beep + screen flash when a detection appears
  (works with person/face/any detection model).
- `tracker.lua` — poll-driven: draw the best box live on the LCD.
- `skeleton.lua` — stick figure from the 17 pose keypoints (pose model).

## 6. Verbose serial logging (Matt: "eyeball what is coming out of these models")

Driver logs at 115200, default ON via config flag:

- At `begin()`/link-up: model name + info string from SSCMA.
- Per frame: one header line (`[vision] kind=pose n=2 perf=12/85/3ms`) and one
  line per detection — boxes/classes/points with all fields; pose adds one
  compact keypoints line per person (`kp: 0:(112,40,89) 1:(118,36,91) …`).
  Full detail, not just the condensed event — the point is seeing what each
  model emits.
- Empty frames log a single quiet line only when the scene transitions to
  empty (mirrors event behavior) so the console stays readable.
- Link transitions and invoke errors always log.

## 7. Testing

- The kind-classification + event-condensing logic lives in a small pure
  helper (no SSCMA/Wire includes) so it *can* get native tests cheaply; add
  them if the helper ends up non-trivial, otherwise compile-check suffices
  for the spike.
- `pio run -e m5sticks3` must pass; add the env to `tools/run-tests.py`'s
  hardcoded `PLATFORMIO_EXAMPLES` list so CI builds it.
- Hardware behavior (I2C link, real models, log output) verified on Matt's
  bench **before committing** (house rule: compile-passing isn't enough for
  `examples/*/src/` changes).

## 8. Out of scope

- No server component.
- No runtime model switching (SenseCraft reflashes the module over USB).
- No semantic gesture events in firmware (wave detection etc. is Lua's job
  now; promote to firmware later if a pattern sticks).
- No support for the C Plus2 board in this example.
