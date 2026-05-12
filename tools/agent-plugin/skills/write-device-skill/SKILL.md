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

## Audience

DEVICE-SKILL.md is written for **Lua app and shader authors** targeting
the device sandbox. The reader is someone composing a Lua app or a
shader expression and pushing it to a running device — they do not have
the firmware source open and cannot change it. Frame everything from
that perspective: the runtime contract (what modules and variables
exist, what they do, what ranges are valid). Do NOT describe the C++
firmware layer — no `DeviceConfig` fields, no driver registration, no
"to enable X the firmware would need to..." asides. If a capability
isn't exposed to Lua, just say so plainly and move on.

## What you need

1. **Firmware project root** — defaults to `cwd`. Ask if not in one.
2. **Device identity** — short name (e.g. "M5StickC Plus2", "Hawthorn lamp").
3. **Driver surface** — which `Resident::Driver` / `Resident::Extension`
   subclasses the firmware registers, and what each one's `name()` and
   Lua module functions are.

## Workflow

1. Read the template at
   `${CLAUDE_PLUGIN_ROOT}/skills/write-device-skill/tools/template.md`.
   `${CLAUDE_PLUGIN_ROOT}` is set by Claude Code to the absolute path of
   the installed plugin — always use it to reference bundled files; the
   CWD is the user's project, not the skill directory.
2. Read the reference example at
   `${CLAUDE_PLUGIN_ROOT}/skills/write-device-skill/tools/example.md` —
   that's the seeded M5StickC Plus2 DEVICE-SKILL.md, useful as a quality
   bar.
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
   - Optional: a `## Validation stubs` Lua block (see template.md and
     example.md for format). This makes `validate-app` produce realistic
     getter return values instead of `nil`, so apps that compute against
     `screen.width()` etc. validate cleanly. Skip if the device has no
     numeric or multi-value getters.
   - App mode vs. shader mode. Always include the `## App mode /
     Shader mode` section. Establish (by asking the user, or by
     checking the firmware source if you have it) whether shader
     mode is exposed to Lua authors on this device. If yes, find
     out (a) what the evaluated expression's return value controls
     — pixel colour, servo angle, fan speed, fill colour, etc.; (b)
     what extra variables are in scope on each evaluation beyond
     the sandbox-generic `ctx.time_ms` / `ctx.trigger_count` / time
     fields and the shader-compatible globals — typically things
     like a pixel index `i`, a strip length `n`, or normalised
     coordinates. Document those as the runtime contract from the
     app author's point of view. If shader mode is not available,
     say so in one sentence and stop — do not explain what the
     firmware would need to change.
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
