---
name: validate-app
description: >-
  Use to validate a Resident Lua app locally before pushing to a device.
  Runs the app through a local lua interpreter with auto-deduced stubs and
  checks for compile errors, missing lifecycle functions, and runtime
  errors during a few simulated ticks. Triggered by /resident:validate-app
  or when the user asks "validate this app".
---

# validate-app

Run a Resident Lua app file through the local `lua` interpreter with
permissive stubs to catch compile errors, missing lifecycle, and obvious
runtime bugs before sending it to a device.

## What you need

1. **A Lua app file** — pass as positional arg, or pipe via stdin.
2. **A DEVICE-SKILL.md** — read from the firmware project root (cwd by
   default). Used to deduce which device modules to stub. Optional: if
   absent, only sandbox-generic stubs are applied.
3. **`lua`** — the Lua interpreter must be on PATH. If missing, the skill
   exits with a clear hint to install it (`brew install lua`).
4. **Optional `--ref <path>` flags** — additional Markdown files
   describing extra modules. validate-app processes each ref file the
   same way as DEVICE-SKILL.md: deduces module names from fenced Lua
   code blocks and appends any `## Validation stubs` block. Repeatable.
   Plain `.lua` refs (no Markdown structure) are silently ignored.
   Useful when an app uses sandbox extensions documented outside
   DEVICE-SKILL.md.

## Usage

```bash
"${CLAUDE_PLUGIN_ROOT}/skills/validate-app/tools/validate.sh" path/to/app.lua
cat app.lua | "${CLAUDE_PLUGIN_ROOT}/skills/validate-app/tools/validate.sh"
"${CLAUDE_PLUGIN_ROOT}/skills/validate-app/tools/validate.sh" --device-skill path/to/DEVICE-SKILL.md path/to/app.lua
"${CLAUDE_PLUGIN_ROOT}/skills/validate-app/tools/validate.sh" --device-skill path/to/DEVICE-SKILL.md --ref path/to/extensions.md path/to/app.lua
```

`${CLAUDE_PLUGIN_ROOT}` is set by Claude Code to the absolute path of the
installed plugin — always use it to reference bundled tools; the CWD is
the user's project, not the skill directory.

Exit codes:
- `0` — passed (compile + lifecycle + 5 ticks all OK)
- `1` — validation failed (single-line error to stderr)
- `2` — environment error (lua missing, file not found, etc.)

## Scope

The validator does NOT run the actual device firmware. It uses **loose
stubs**:
- Sandbox built-ins (`log.*`, `time.*`, math globals, shader helpers) are
  hardcoded with neutral return values.
- Device modules (whatever DEVICE-SKILL.md mentions) get a permissive
  metatable that returns a no-op function for any access.

This catches: syntax errors, missing lifecycle, obvious type errors and
nil-dereferences. It does not catch: hardware-specific behavior, timing
issues, or modules referenced in code but absent from DEVICE-SKILL.md
(those will fall back to a global `__index` no-op trap).

## Optional: validation stubs in DEVICE-SKILL.md

If auto-deduced stubs are too loose for your apps (e.g. apps do
arithmetic on the result of `screen.width()`), add a `## Validation stubs`
section to your DEVICE-SKILL.md with a Lua block that provides concrete
return values for getter-style functions. validate-app injects that
block after the auto-deduced stubs, so it overrides them.

Example (m5stick-demo's DEVICE-SKILL.md):

```lua
screen = setmetatable({
  width  = function() return 240 end,
  height = function() return 135 end,
}, { __index = function() return function() end end })

imu = setmetatable({
  accel = function() return 0, 0, 1 end,
  gyro  = function() return 0, 0, 0 end,
}, { __index = function() return function() end end })
```

## Composing with other skills

`validate-app` is independent. The expected pattern is:

1. `create-app --out apps/foo.lua "<description>"`
2. `validate-app apps/foo.lua` → if fails, re-prompt create-app with the
   error and iterate.
3. `push-app --base-url <url> --device-id <id> apps/foo.lua`
