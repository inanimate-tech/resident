# Plan B — DEVICE-SKILL.md format + seeded m5stick example + write-device-skill skill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the DEVICE-SKILL.md format by (a) seeding `examples/m5stick-demo/DEVICE-SKILL.md` from the existing hawthorn `stick-app.md`, scoped to the m5stick-demo's actual driver surface and stripped of sandbox-generic content; (b) adding a `write-device-skill` skill under `tools/agent-plugin/skills/` that bundles a template and reference example to help authors write a DEVICE-SKILL.md for any new firmware project.

**Architecture:** No code — Markdown documents and skill metadata. The skill's `SKILL.md` describes how to interview the user and produce a DEVICE-SKILL.md at the firmware project root. The seed example is hand-edited from `~/code/hawthorn-worker/agents/prompts/stick-app.md` to drop hawthorn-specific content and resident-sandbox-generic content.

**Tech Stack:** Markdown only.

**Spec reference:** `~/code/resident/docs/superpowers/specs/2026-05-07-resident-sandbox-tooling-design.md` § "DEVICE-SKILL.md format" and § "Skills > write-device-skill".

**Working directory for this plan:** `~/code/resident/`.

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `examples/m5stick-demo/DEVICE-SKILL.md` | create | Seed reference DEVICE-SKILL.md |
| `tools/agent-plugin/skills/write-device-skill/SKILL.md` | create | Skill metadata + workflow |
| `tools/agent-plugin/skills/write-device-skill/tools/template.md` | create | Empty DEVICE-SKILL.md template |
| `tools/agent-plugin/skills/write-device-skill/tools/example.md` | create | Reference example (copy of m5stick DEVICE-SKILL.md) |
| `tools/agent-plugin/.claude-plugin/plugin.json` | modify | Bump plugin version to 0.3.0 |

---

### Task 1: Seed examples/m5stick-demo/DEVICE-SKILL.md

**Files:**
- Create: `~/code/resident/examples/m5stick-demo/DEVICE-SKILL.md`

The seed is adapted from `~/code/hawthorn-worker/agents/prompts/stick-app.md`. Drop:
- The opening "obedient expert coder" preamble (we don't bake voice into the doc).
- The "App events / room.announce / coordination" guidance (hawthorn-specific).
- The "Lua App Runtime Reference" overview pointer (sandbox-generic — covered by `create-app`'s embedded docs).
- The `## Context` section (sandbox-generic).
- All references to `kv.*`, `time.*`, `log.*` (sandbox-generic — `create-app` knows about these).
- The "When writing apps, always consider..." closing checklist (lifecycle reminders are sandbox-generic).

Keep:
- Hardware description (TFT, IMU, buzzer, buttons).
- The `screen.*`, `imu.*`, `buzzer.*`, `button.*` reference.
- The examples (rewrite/keep only those that exercise device modules; drop ones that mostly use sandbox built-ins).
- Constraints (screen/buzzer ranges).
- Practical tips that are device-specific (screen.flip MUST, button index, screen orientation).

- [ ] **Step 1: Write the file**

```markdown
# M5StickC Plus2

The M5StickC Plus2 is a wrist-mountable ESP32 device with a 135×240 colour TFT,
a 6-axis IMU, two physical buttons, and a piezo buzzer. Apps drive the screen
as their primary output and respond to button presses + motion.

## Hardware

**TFT screen:** ST7789V2, 135×240 pixels, landscape orientation (240 wide,
135 tall). Coordinates 0-based, origin top-left. Colours 0–255 per channel.
Double-buffered: draw to an off-screen sprite, then `screen.flip()` to push
the frame.

**IMU:** 6-axis (MPU6886). Body frame: X = long axis (USB toward −X), Y =
short axis, Z = screen normal (positive points out of the screen toward the
viewer). At rest face-up, `accel()` returns approximately `(0, 0, +1)` in g.
Gyro is in degrees/second.

**Buzzer:** Piezo. Frequency range 100–8000 Hz, duration 10–5000 ms.

**Buttons:** Two physical buttons, indexed 0 and 1. Surface as `button` events
with an `index` field.

## Lua Modules

### screen.*
**Hardware:** ST7789V2 TFT, 240×135 landscape, double-buffered.

```lua
-- Clear the off-screen sprite
screen.clear()                                   -- clear to black
screen.clear(255, 0, 0)                          -- clear to red

-- Draw text (defaults: size 2, white)
screen.text(10, 10, "HELLO")                     -- size 2, white
screen.text(10, 10, "HELLO", 3)                  -- size 3, white
screen.text(10, 10, "HELLO", 2, 255, 0, 0)       -- size 2, red

-- Draw a QR code (auto-picks QR v3..v10, ECC low). Caller is responsible
-- for a light background behind the QR for scannability.
screen.qr(62, 10, "https://example.com")          -- scale 4, black
screen.qr(62, 10, "https://example.com", 3)       -- scale 3, black
screen.qr(62, 10, "https://example.com", 4, 0, 0, 128)  -- navy

-- Shapes
screen.fill_rect(x, y, w, h, r, g, b)            -- filled rectangle
screen.rect(x, y, w, h, r, g, b)                 -- 1px outline rectangle
screen.line(x0, y0, x1, y1, r, g, b)             -- 1px line
screen.triangle(x0, y0, x1, y1, x2, y2, r, g, b) -- 1px outline triangle
screen.fill_triangle(x0, y0, x1, y1, x2, y2, r, g, b)
screen.pixel(x, y, r, g, b)                      -- single pixel

-- Push the sprite to the display (call once per frame)
screen.flip()

-- Settings
screen.set_brightness(128)                       -- 0–255

-- Query dimensions
local w = screen.width()                         -- 240
local h = screen.height()                        -- 135
```

**MUST:** call `screen.flip()` after every draw sequence — nothing is
visible until you flip. On app reset the screen is cleared to black.

### imu.*
**Hardware:** MPU6886 6-axis.

```lua
local ax, ay, az = imu.accel()  -- g-force, body frame
local gx, gy, gz = imu.gyro()   -- degrees/second, body frame
local t          = imu.temp()   -- stub, returns 0
```

Gyro measures rate, so readings spike during motion and return to ~0 at
rest. For absolute orientation use `accel()` (tilt) — yaw around +Z is not
observable without a magnetometer.

### buzzer.*
**Hardware:** Piezo.

```lua
buzzer.beep(440, 200)   -- frequency Hz, duration ms
buzzer.tone(1000)       -- start continuous tone
buzzer.stop()           -- stop tone
```

Frequency 100–8000 Hz; duration 10–5000 ms. Tone is stopped on app reset.

### button.*
**Hardware:** 2 physical buttons, indices 0 and 1.

```lua
local count = button.press_count()  -- total presses since boot
```

Best practice: handle button presses in `on_event(ctx, e)` rather than
polling. The event has a `name == "button"` and an `index` field (0 or 1).

## Examples

**Hello world:**

```lua
function init(ctx)
  screen.clear()
  screen.text(10, 10, "HELLO WORLD")
  screen.flip()
end
```

**Bouncing ball:**

```lua
local x, y = 120, 67
local dx, dy = 2, 1.5
local r = 8

function on_tick(ctx, dt_ms)
  x = x + dx
  y = y + dy
  if x <= r or x >= 240 - r then dx = -dx end
  if y <= r or y >= 135 - r then dy = -dy end

  screen.clear()
  screen.fill_rect(math.floor(x - r), math.floor(y - r), r * 2, r * 2, 0, 255, 0)
  screen.flip()
end
```

**Spirit level:**

```lua
function on_tick(ctx, dt_ms)
  local ax, ay, az = imu.accel()
  screen.clear()

  local cx = math.floor(120 + ax * 100)
  local cy = math.floor(67 + ay * 100)
  cx = math.max(5, math.min(235, cx))
  cy = math.max(5, math.min(130, cy))

  screen.fill_rect(118, 0, 4, 135, 50, 50, 50)
  screen.fill_rect(0, 65, 240, 4, 50, 50, 50)
  screen.fill_rect(cx - 5, cy - 5, 10, 10, 0, 255, 0)
  screen.flip()
end
```

**Two-button counter with beep:**

```lua
local count = 0

function init(ctx)
  draw()
end

function draw()
  screen.clear()
  screen.text(10, 10, "Count: " .. count)
  screen.text(10, 50, "Btn0: +1")
  screen.text(10, 70, "Btn1: reset")
  screen.flip()
end

function on_event(ctx, e)
  if e.name == "button" then
    if e.index == 0 then
      count = count + 1
      buzzer.beep(440, 50)
    else
      count = 0
      buzzer.beep(880, 50)
    end
    draw()
  end
end
```

**Shake detection:**

```lua
local shake_threshold = 2.0

function on_tick(ctx, dt_ms)
  local ax, ay, az = imu.accel()
  local mag = math.sqrt(ax*ax + ay*ay + az*az)
  if mag > shake_threshold then
    buzzer.beep(800, 100)
    screen.clear(255, 0, 0)
  else
    screen.clear(0, 50, 0)
  end
  screen.flip()
end
```

## Constraints

- Screen: 240×135 landscape, 0-based coords, 0–255 colour channels.
- IMU: g-force for accel, deg/sec for gyro. `temp()` stubbed.
- Buzzer: 100–8000 Hz, 10–5000 ms.
- Two buttons only — indices 0 and 1.

## Practical Tips

- The screen is the primary output surface. Don't rely on the LED.
- Display orientation is landscape (240 wide × 135 tall). Authors who
  forget this draw vertical apps that look wrong.
- Always call `screen.flip()` after a draw sequence — double-buffered.
- Use `e.index` (0 or 1) to distinguish buttons in `on_event`.
- Precompute lookup tables in `init()` to avoid math in hot paths.
- For shake detection, threshold against magnitude, not individual axes.
```

- [ ] **Step 2: Sanity-check the file renders cleanly**

Run: `head -40 ~/code/resident/examples/m5stick-demo/DEVICE-SKILL.md`
Expected: heading + paragraph + Hardware section visible.

- [ ] **Step 3: Commit**

```bash
cd ~/code/resident
git add examples/m5stick-demo/DEVICE-SKILL.md
git commit -m "docs(m5stick-demo): seed DEVICE-SKILL.md from hawthorn stick-app.md"
```

---

### Task 2: Create write-device-skill skill scaffolding

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/write-device-skill/SKILL.md`
- Create: `~/code/resident/tools/agent-plugin/skills/write-device-skill/tools/template.md`
- Create: `~/code/resident/tools/agent-plugin/skills/write-device-skill/tools/example.md`

- [ ] **Step 1: Create the skill directory**

Run:
```bash
mkdir -p ~/code/resident/tools/agent-plugin/skills/write-device-skill/tools
```

Expected: directory exists. Verify with `ls -d ~/code/resident/tools/agent-plugin/skills/write-device-skill/tools`.

- [ ] **Step 2: Write the template file**

Create `~/code/resident/tools/agent-plugin/skills/write-device-skill/tools/template.md`:

```markdown
# <Device name>

<one-paragraph overview: what the device is, what kind of apps suit it>

## Hardware

<physical description of each piece of hardware: screens, sensors, audio,
indicators, buttons. Include coordinate frames and units the Lua side
will see (g-force, Hz, ms, etc.).>

## Lua Modules

### <module-name>.*
**Hardware:** <one-line>

\`\`\`lua
-- Function-by-function reference: signatures, defaults, ranges.
\`\`\`

(Repeat per driver module the firmware registers.)

## Examples

<3–6 short, working Lua apps that exercise the device modules. Tight.>

## Constraints

<screen dimensions, frequency/duration ranges, memory limits — anything
that shapes generated code.>

## Practical Tips

<device-specific idioms (shake detection, menu nav, etc.). Optional.>
```

- [ ] **Step 3: Copy the m5stick DEVICE-SKILL.md as the reference example**

Run:
```bash
cp ~/code/resident/examples/m5stick-demo/DEVICE-SKILL.md \
   ~/code/resident/tools/agent-plugin/skills/write-device-skill/tools/example.md
```

Verify: `wc -l ~/code/resident/tools/agent-plugin/skills/write-device-skill/tools/example.md` shows >100 lines.

- [ ] **Step 4: Write the SKILL.md**

Create `~/code/resident/tools/agent-plugin/skills/write-device-skill/SKILL.md`:

```markdown
---
name: write-device-skill
description: >-
  Use when authoring a DEVICE-SKILL.md for a new Resident-based firmware
  project. Walks the user through hardware, Lua module surface, examples,
  and constraints. Triggered when the user says "write a device skill",
  "set up DEVICE-SKILL.md", or invokes /resident:write-device-skill.
---

# write-device-skill

Help the user produce a `DEVICE-SKILL.md` for a Resident-based firmware
project. The output is a single Markdown file at the firmware project root
that documents the device-specific Lua surface — hardware, modules,
examples, constraints. Sandbox-generic content (lifecycle callbacks, ctx,
log, time, kv, math globals) is NOT in DEVICE-SKILL.md; it lives in
Resident's own docs and is loaded by the create-app skill.

## What you need

1. **Firmware project root** — defaults to `cwd`. Ask if not in one.
2. **Device identity** — short name (e.g. "M5StickC Plus2", "Hawthorn lamp").
3. **Driver surface** — which `Resident::Driver` / `Resident::Extension`
   subclasses the firmware registers, and what each one's `name()` and
   Lua module functions are.

## Workflow

1. Read the template at `tools/template.md` (this skill's directory).
2. Read the reference example at `tools/example.md` — that's the seeded
   M5StickC Plus2 DEVICE-SKILL.md, useful as a quality bar.
3. Ask the user (one question at a time):
   - Device name + a one-paragraph overview.
   - Hardware list (screen? sensors? audio? buttons? indicators?).
     For each, ask for size/units/coordinate frames.
   - Driver modules: which Lua module names are exposed (e.g. `screen`,
     `imu`)? For each, ask the user to walk through the available
     functions, signatures, and any defaults/ranges.
   - 3–6 example apps that exercise the modules. The agent can draft
     these from the hardware description; user confirms.
   - Constraints (dimensions, ranges, memory).
   - Optional: device-specific practical tips.
4. Write the result to `<project-root>/DEVICE-SKILL.md`. Do NOT include
   sandbox-generic content (lifecycle, ctx, log/time/kv, math globals).
5. Show the user a diff/summary and confirm before writing.

## Pointers when interviewing

- A driver's Lua module name is whatever its `Extension::name()` returns
  in C++. If the user has firmware source open, point them at the `Driver`
  subclass declarations.
- Module function signatures come from `LuaModule::method<>` /
  `staticMethod` / `constant` calls in `registerModule()`.
- For sensors that return multiple values (e.g. `imu.accel()` returning
  `ax, ay, az`), Lua-style multi-return is the convention.
- Coordinate frames matter — document body frame, screen orientation,
  and the sign conventions of any axis.

## What NOT to put in DEVICE-SKILL.md

These are sandbox-generic and live in Resident's own docs (loaded by
create-app). DON'T duplicate them per device:

- App lifecycle: `init(ctx)`, `on_tick(ctx, dt_ms)`, `on_event(ctx, event)`
- The `ctx` table fields (time_ms, trigger_count, utc_h/m, etc.)
- Built-in modules: `log.*`, `time.*`, `kv.*` (if present)
- Shader-compatible globals: `rgb`, `fract`, `beat`, `noise2d`
- Math globals: `floor`, `ceil`, `abs`, `sin`, `cos`, etc.

If the firmware exposes a *device-specific* module that overlaps with one
of these names, document it under Lua Modules — but don't restate the
universal modules.
```

- [ ] **Step 5: Bump the plugin version**

Edit `~/code/resident/tools/agent-plugin/.claude-plugin/plugin.json`. Read it first to see current version, then update.

Read it:
```bash
cat ~/code/resident/tools/agent-plugin/.claude-plugin/plugin.json
```

Expected: shows current version (0.2.0 per recent commit).

Edit the file: change `"version": "0.2.0"` to `"version": "0.3.0"`.

- [ ] **Step 6: Verify file structure**

Run:
```bash
find ~/code/resident/tools/agent-plugin/skills/write-device-skill -type f
```

Expected output (3 files):
```
.../write-device-skill/SKILL.md
.../write-device-skill/tools/template.md
.../write-device-skill/tools/example.md
```

- [ ] **Step 7: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/write-device-skill \
        tools/agent-plugin/.claude-plugin/plugin.json
git commit -m "feat(agent-plugin): add write-device-skill skill with template + reference"
```

---

### Task 3: Smoke-test write-device-skill via plugin invocation

**Files:** none modified — manual verification.

- [ ] **Step 1: Start a Claude Code session with the plugin loaded**

Run:
```bash
cd /tmp && mkdir -p test-device-project && cd test-device-project
claude --plugin-dir ~/code/resident/tools/agent-plugin
```

In the session, type: `/resident:write-device-skill`.

Expected: skill loads (you see "Launching skill: write-device-skill" or it activates inline). Claude reads template + example, asks the first interview question.

If skill name doesn't match, check `SKILL.md` frontmatter `name:` field is `write-device-skill`.

- [ ] **Step 2: Walk through one short interview**

Provide minimal answers (e.g. "It's a one-button gadget with a single LED"). Confirm the produced `DEVICE-SKILL.md` lands at `/tmp/test-device-project/DEVICE-SKILL.md` and excludes sandbox-generic content (no lifecycle section, no `ctx` section, no `log`/`time` references).

- [ ] **Step 3: Clean up**

Run: `rm -rf /tmp/test-device-project`

No commit — this was manual verification.

---

## Self-review

- **Spec coverage:** DEVICE-SKILL.md format § matches Task 1 layout. Skills > write-device-skill § matches Task 2 (workflow, template + reference example, output path). The "v1 scope: user describes their device; no C++ parsing" caveat is reflected in SKILL.md's wording.
- **Placeholders:** none — full Markdown content for both DEVICE-SKILL.md and the SKILL.md.
- **Type consistency:** module names (`screen`, `imu`, `buzzer`, `button`) consistent across the seed and template/example.
