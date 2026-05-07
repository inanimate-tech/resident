# Plan C — validate-app skill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `validate-app` skill under `tools/agent-plugin/skills/` that runs a Lua app file (or stdin) through the local `lua` binary with auto-deduced stubs, checking compile, lifecycle presence, and a few simulated `init` + `on_tick` cycles.

**Architecture:**
- A shell script (`tools/validate.sh`) does CLI parsing, locates `lua`, parses `DEVICE-SKILL.md` to deduce module names, and assembles a harness Lua file by concatenating: hardcoded sandbox stubs → auto-deduced device-module stubs → user app → harness driver. Then invokes `lua harness.lua` and translates the exit code/stderr to a structured pass/fail.
- Module-name deduction: scan Lua code blocks (lines fenced by ```lua) in DEVICE-SKILL.md, extract top-level identifiers used as `<ident>.<member>` or `<ident>(`. Built-in identifiers (`math`, `string`, `table`, plus the sandbox built-ins covered by hardcoded stubs) are filtered out so they don't override the real implementations.

**Tech Stack:** Bash, `lua` binary (5.4 is what Resident's sandbox uses on-device; any 5.x works for syntax + stub-runtime checks), standard Unix tools (`grep`, `awk`).

**Spec reference:** `~/code/resident/docs/superpowers/specs/2026-05-07-resident-sandbox-tooling-design.md` § "Skills > validate-app".

**Working directory for this plan:** `~/code/resident/`.

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `tools/agent-plugin/skills/validate-app/SKILL.md` | create | Skill metadata + workflow |
| `tools/agent-plugin/skills/validate-app/tools/validate.sh` | create | Entry-point CLI |
| `tools/agent-plugin/skills/validate-app/tools/builtins.lua` | create | Hardcoded sandbox built-in stubs (log, time, math globals, shader helpers) |
| `tools/agent-plugin/skills/validate-app/tools/harness.lua` | create | Driver: defines `ctx`, asserts lifecycle presence, calls `init` + `on_tick` × 5 |
| `tools/agent-plugin/skills/validate-app/tools/deduce-modules.sh` | create | Helper: extract device module names from a DEVICE-SKILL.md |

A test fixture lives under `tools/agent-plugin/skills/validate-app/tests/` with sample apps and a tiny test harness.

| File | Action | Purpose |
|------|--------|---------|
| `tools/agent-plugin/skills/validate-app/tests/run.sh` | create | Run all fixtures and assert pass/fail per file |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/ok-init-only.lua` | create | App with only `init` defined — should pass |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/ok-bouncing-ball.lua` | create | Realistic app using screen.* — should pass |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/fail-no-lifecycle.lua` | create | App with no init/on_tick/on_event — should fail |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/fail-syntax-error.lua` | create | App with a Lua syntax error — should fail |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/fail-runtime-error.lua` | create | App that errors inside on_tick — should fail |
| `tools/agent-plugin/skills/validate-app/tests/fixtures/DEVICE-SKILL.md` | create | Minimal DEVICE-SKILL.md mentioning `screen` and `imu` |

---

### Task 1: Create skill scaffolding and the builtins/harness Lua files

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/SKILL.md`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tools/builtins.lua`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tools/harness.lua`

- [ ] **Step 1: Make the directory structure**

```bash
mkdir -p ~/code/resident/tools/agent-plugin/skills/validate-app/tools
mkdir -p ~/code/resident/tools/agent-plugin/skills/validate-app/tests/fixtures
```

- [ ] **Step 2: Write SKILL.md**

Create `~/code/resident/tools/agent-plugin/skills/validate-app/SKILL.md`:

```markdown
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

## Usage

```bash
./tools/validate.sh path/to/app.lua
cat app.lua | ./tools/validate.sh
./tools/validate.sh --device-skill path/to/DEVICE-SKILL.md path/to/app.lua
```

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

## Composing with other skills

`validate-app` is independent. The expected pattern is:

1. `create-app --out apps/foo.lua "<description>"`
2. `validate-app apps/foo.lua` → if fails, re-prompt create-app with the
   error and iterate.
3. `push-app --base-url <url> --device-id <id> apps/foo.lua`
```

- [ ] **Step 3: Write builtins.lua**

Create `~/code/resident/tools/agent-plugin/skills/validate-app/tools/builtins.lua`:

```lua
-- Hardcoded stubs for the Resident sandbox built-in surface.
-- Loaded before the user's app so calls don't crash with nil-derefs.

log = {
  info  = function(_) end,
  warn  = function(_) end,
  error = function(_) end,
}

time = {
  is_valid     = function() return true end,
  has_timezone = function() return false end,
  hour         = function() return 12 end,
  minute       = function() return 0 end,
  second       = function() return 0 end,
  day_id       = function() return 1 end,
}

-- kv may or may not be present on a given device, but apps that use it
-- need a non-crashing stub. Returns nil from get; true from set.
kv = {
  get = function(_) return nil end,
  set = function(_, _) return true end,
}

-- Shader-compatible globals (also valid in apps).
function rgb(_, _, _) return -1 end       -- negative integer = "this is a color"
function fract(x) return x - math.floor(x) end
function beat(bpm, t) return t / (60000 / bpm) end
function noise2d(_, _) return 0 end

-- Math globals registered without the math. prefix.
floor = math.floor
ceil  = math.ceil
abs   = math.abs
sin   = math.sin
cos   = math.cos
tan   = math.tan
sqrt  = math.sqrt
min   = math.min
max   = math.max
fmod  = math.fmod
```

- [ ] **Step 4: Write harness.lua**

Create `~/code/resident/tools/agent-plugin/skills/validate-app/tools/harness.lua`:

```lua
-- Driver that runs after builtins + device stubs + user app are loaded.
-- The user app's globals (init, on_tick, on_event) are now in _G.

local ctx = {
  time_ms       = 0,
  trigger_count = 0,
  utc_h         = 12,
  utc_m         = 0,
  localtime_h   = 12,
  localtime_m   = 0,
  day_id        = 1,
}

if not (init or on_tick or on_event) then
  io.stderr:write("validate-app: FAIL: app defines none of init / on_tick / on_event\n")
  os.exit(1)
end

if init then
  local ok, err = pcall(init, ctx)
  if not ok then
    io.stderr:write("validate-app: FAIL: init: " .. tostring(err) .. "\n")
    os.exit(1)
  end
end

if on_tick then
  for _ = 1, 5 do
    ctx.time_ms = ctx.time_ms + 100
    local ok, err = pcall(on_tick, ctx, 100)
    if not ok then
      io.stderr:write("validate-app: FAIL: on_tick: " .. tostring(err) .. "\n")
      os.exit(1)
    end
  end
end

-- on_event is not invoked in v1 (would require synthesizing events).
io.stdout:write("validate-app: OK\n")
os.exit(0)
```

- [ ] **Step 5: Verify the harness compiles standalone with builtins**

This isn't a real test — just sanity check the Lua files parse. Requires `lua` on PATH.

```bash
cd ~/code/resident/tools/agent-plugin/skills/validate-app/tools
luac -p builtins.lua
luac -p harness.lua
```

Expected: both succeed silently (exit 0). If `luac` is missing, install with `brew install lua`.

- [ ] **Step 6: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/validate-app/
git commit -m "feat(agent-plugin): scaffold validate-app skill (SKILL.md, builtins, harness)"
```

---

### Task 2: Implement the module-deduction helper

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tools/deduce-modules.sh`

The helper reads a DEVICE-SKILL.md and emits one module name per line on
stdout. A module name is a top-level identifier that appears as
`<ident>.<member>` or `<ident>(` inside a fenced ```lua code block, that
isn't already a sandbox built-in or a Lua built-in.

- [ ] **Step 1: Write the script**

Create `~/code/resident/tools/agent-plugin/skills/validate-app/tools/deduce-modules.sh`:

```bash
#!/usr/bin/env bash
#
# deduce-modules.sh DEVICE-SKILL.md
#   Reads DEVICE-SKILL.md, extracts top-level identifiers used as
#   <ident>.<member> or <ident>(<args>) inside ```lua code blocks, and
#   emits the unique device-specific module names on stdout, one per line.
#
# Filters out sandbox built-ins (which are stubbed by builtins.lua) and
# Lua built-ins (which the real interpreter provides).

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 DEVICE-SKILL.md" >&2
  exit 2
fi

src="$1"
if [[ ! -f "$src" ]]; then
  echo "Error: file not found: $src" >&2
  exit 2
fi

# Identifiers we never emit (already provided by builtins.lua or Lua itself).
exclude='^(log|time|kv|rgb|fract|beat|noise2d|floor|ceil|abs|sin|cos|tan|sqrt|min|max|fmod|math|string|table|io|os|coroutine|debug|package|tostring|tonumber|type|pairs|ipairs|next|select|error|assert|pcall|xpcall|setmetatable|getmetatable|rawset|rawget|rawequal|rawlen|require|print|unpack|_G|_VERSION|init|on_tick|on_event|ctx|dt_ms|e|event|self|true|false|nil|local|function|end|if|then|else|elseif|for|do|while|repeat|until|break|return|in|and|or|not)$'

awk '
  /^```lua/ { in_block = 1; next }
  /^```/    { in_block = 0; next }
  in_block  { print }
' "$src" \
  | grep -oE '\b[a-zA-Z_][a-zA-Z0-9_]*\b(\.|[(])' \
  | sed 's/[.(]$//' \
  | sort -u \
  | grep -Ev "$exclude" || true
```

- [ ] **Step 2: Make it executable**

```bash
chmod +x ~/code/resident/tools/agent-plugin/skills/validate-app/tools/deduce-modules.sh
```

- [ ] **Step 3: Test it against the seeded m5stick DEVICE-SKILL.md**

Run:
```bash
~/code/resident/tools/agent-plugin/skills/validate-app/tools/deduce-modules.sh \
  ~/code/resident/examples/m5stick-demo/DEVICE-SKILL.md
```

Expected output (sorted):
```
button
buzzer
imu
screen
```

(Plus possibly a few false positives like `draw` if a function named
`draw` is called inside an example. That's OK — they get stubbed
permissively and have no effect on validation.)

- [ ] **Step 4: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/validate-app/tools/deduce-modules.sh
git commit -m "feat(validate-app): add deduce-modules.sh helper"
```

---

### Task 3: Implement the validate.sh entry point

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tools/validate.sh`

- [ ] **Step 1: Write the script**

Create `~/code/resident/tools/agent-plugin/skills/validate-app/tools/validate.sh`:

```bash
#!/usr/bin/env bash
#
# validate.sh — validate a Resident Lua app locally.
#
# Usage:
#   validate.sh path/to/app.lua
#   cat app.lua | validate.sh
#   validate.sh --device-skill path/to/DEVICE-SKILL.md path/to/app.lua
#
# Exit codes:
#   0  — passed
#   1  — validation failed (line written to stderr)
#   2  — environment error (lua missing, file not found, etc.)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILTINS="$SCRIPT_DIR/builtins.lua"
HARNESS="$SCRIPT_DIR/harness.lua"
DEDUCE="$SCRIPT_DIR/deduce-modules.sh"

device_skill=""
app_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device-skill) device_skill="$2"; shift 2 ;;
    -h|--help)
      cat <<EOF
Usage: $0 [--device-skill PATH] [APP_FILE]
       cat app.lua | $0 [--device-skill PATH]

Validates a Resident Lua app locally. Reads from APP_FILE or stdin.
EOF
      exit 0
      ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  app_file="$1"; shift ;;
  esac
done

if ! command -v lua >/dev/null 2>&1; then
  echo "validate-app: error: 'lua' not found on PATH." >&2
  echo "Install with: brew install lua" >&2
  exit 2
fi

if [[ -z "$device_skill" && -f "./DEVICE-SKILL.md" ]]; then
  device_skill="./DEVICE-SKILL.md"
fi

if [[ -n "$app_file" ]]; then
  if [[ ! -f "$app_file" ]]; then
    echo "validate-app: error: file not found: $app_file" >&2
    exit 2
  fi
  app_code=$(cat "$app_file")
elif [[ ! -t 0 ]]; then
  app_code=$(cat)
else
  echo "validate-app: error: no app provided. Pass a file path or pipe via stdin." >&2
  exit 2
fi

# Build device-module stubs by deducing module names from DEVICE-SKILL.md.
# Each module gets a permissive metatable.
device_stubs=""
if [[ -n "$device_skill" && -f "$device_skill" ]]; then
  while IFS= read -r module; do
    [[ -z "$module" ]] && continue
    device_stubs+=$'\n'"$module = setmetatable({}, { __index = function() return function() end end })"
  done < <("$DEDUCE" "$device_skill")
fi

# A catch-all metatable on _G so any unknown global access also gets a no-op.
# Apps that reference truly unknown things won't crash on simple .x access.
fallback_stub='
setmetatable(_G, { __index = function(_, _) return setmetatable({}, { __index = function() return function() end end }) end })
'

# Compose harness via a temp file (preserves line numbers in errors poorly,
# but is robust to embedded newlines / quoting).
tmp=$(mktemp -t validate-app.XXXXXX.lua)
trap 'rm -f "$tmp"' EXIT

{
  cat "$BUILTINS"
  echo "$device_stubs"
  echo "$fallback_stub"
  echo '-- ===== USER APP =====' 
  echo "$app_code"
  echo '-- ===== HARNESS ====='
  cat "$HARNESS"
} > "$tmp"

# Run; capture stderr.
if err=$(lua "$tmp" 2>&1 >/dev/null); then
  echo "validate-app: OK"
  exit 0
fi

# `lua` exited non-zero. The harness writes its own structured FAIL line on
# its own errors; if we got here without that prefix, it's likely a Lua-level
# load/compile error. Print whatever stderr we got.
if [[ -n "$err" ]]; then
  echo "$err" >&2
else
  echo "validate-app: FAIL: lua exited non-zero with no stderr" >&2
fi
exit 1
```

- [ ] **Step 2: Make it executable**

```bash
chmod +x ~/code/resident/tools/agent-plugin/skills/validate-app/tools/validate.sh
```

- [ ] **Step 3: Smoke-test against a trivial passing app**

```bash
echo 'function init(ctx) end' | \
  ~/code/resident/tools/agent-plugin/skills/validate-app/tools/validate.sh
```

Expected output to stdout: `validate-app: OK`. Exit 0.

- [ ] **Step 4: Smoke-test against a missing-lifecycle failure**

```bash
echo 'local x = 1' | \
  ~/code/resident/tools/agent-plugin/skills/validate-app/tools/validate.sh
```

Expected stderr: contains `app defines none of init / on_tick / on_event`. Exit 1.

- [ ] **Step 5: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/validate-app/tools/validate.sh
git commit -m "feat(validate-app): add validate.sh entry point with stub composition"
```

---

### Task 4: Add fixtures and a tests/run.sh harness

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tests/fixtures/DEVICE-SKILL.md`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tests/fixtures/ok-init-only.lua`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tests/fixtures/ok-bouncing-ball.lua`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tests/fixtures/fail-no-lifecycle.lua`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tests/fixtures/fail-syntax-error.lua`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tests/fixtures/fail-runtime-error.lua`
- Create: `~/code/resident/tools/agent-plugin/skills/validate-app/tests/run.sh`

- [ ] **Step 1: Create fixtures/DEVICE-SKILL.md**

```markdown
# Test Fixture Device

Minimal DEVICE-SKILL.md for validate-app tests. References two modules:
`screen` and `imu`.

## Hardware

A pretend screen and a pretend IMU.

## Lua Modules

### screen.*

\`\`\`lua
screen.clear()
screen.flip()
local w = screen.width()
\`\`\`

### imu.*

\`\`\`lua
local ax, ay, az = imu.accel()
\`\`\`
```

- [ ] **Step 2: Create the passing fixtures**

`fixtures/ok-init-only.lua`:
```lua
function init(ctx)
  screen.clear()
  screen.flip()
end
```

`fixtures/ok-bouncing-ball.lua`:
```lua
local x, y = 0, 0
local dx, dy = 1, 1

function on_tick(ctx, dt_ms)
  x = x + dx
  y = y + dy
  local ax, ay, az = imu.accel()
  screen.clear()
  screen.flip()
end
```

- [ ] **Step 3: Create the failing fixtures**

`fixtures/fail-no-lifecycle.lua`:
```lua
local x = 1
print(x)
```

`fixtures/fail-syntax-error.lua`:
```lua
function init(ctx
  screen.clear()
end
```

`fixtures/fail-runtime-error.lua`:
```lua
function on_tick(ctx, dt_ms)
  error("boom")
end
```

- [ ] **Step 4: Write tests/run.sh**

```bash
#!/usr/bin/env bash
#
# Run validate-app against each fixture. Pass-fixtures must exit 0,
# fail-fixtures must exit 1.

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIXTURES="$SCRIPT_DIR/fixtures"
VALIDATE="$SCRIPT_DIR/../tools/validate.sh"
SKILL="$FIXTURES/DEVICE-SKILL.md"

fail=0
pass=0

run() {
  local fixture="$1"
  local expected="$2"
  local out err code
  out=$("$VALIDATE" --device-skill "$SKILL" "$fixture" 2>&1)
  code=$?
  if [[ "$expected" == "ok" && "$code" -eq 0 ]]; then
    echo "PASS  $fixture"
    pass=$((pass+1))
  elif [[ "$expected" == "fail" && "$code" -eq 1 ]]; then
    echo "PASS  $fixture (expected fail; exit 1)"
    pass=$((pass+1))
  else
    echo "FAIL  $fixture (expected=$expected, got code=$code)"
    echo "      output: $out"
    fail=$((fail+1))
  fi
}

run "$FIXTURES/ok-init-only.lua"      "ok"
run "$FIXTURES/ok-bouncing-ball.lua"  "ok"
run "$FIXTURES/fail-no-lifecycle.lua" "fail"
run "$FIXTURES/fail-syntax-error.lua" "fail"
run "$FIXTURES/fail-runtime-error.lua" "fail"

echo
echo "$pass passed, $fail failed"
exit $fail
```

- [ ] **Step 5: Make run.sh executable**

```bash
chmod +x ~/code/resident/tools/agent-plugin/skills/validate-app/tests/run.sh
```

- [ ] **Step 6: Run the tests**

```bash
~/code/resident/tools/agent-plugin/skills/validate-app/tests/run.sh
```

Expected output:
```
PASS  .../ok-init-only.lua
PASS  .../ok-bouncing-ball.lua
PASS  .../fail-no-lifecycle.lua (expected fail; exit 1)
PASS  .../fail-syntax-error.lua (expected fail; exit 1)
PASS  .../fail-runtime-error.lua (expected fail; exit 1)

5 passed, 0 failed
```

If any test fails, debug by running validate.sh against the fixture
directly and reading the harness output (`bash -x` on validate.sh shows
the temp file path).

- [ ] **Step 7: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/validate-app/tests/
git commit -m "test(validate-app): add fixtures and run.sh harness; all green"
```

---

### Task 5: Validate the seeded m5stick-demo apps

This is the assumption-test called out in the spec ("stubs deduced from
DEVICE-SKILL.md are sufficient"). Run validate-app against every Lua file
in `examples/m5stick-demo/device-apps/`. They should all pass.

**Files:** none modified.

- [ ] **Step 1: Run validate-app against each m5stick-demo app**

```bash
cd ~/code/resident/examples/m5stick-demo
for f in device-apps/*.lua; do
  echo "--- $f"
  ~/code/resident/tools/agent-plugin/skills/validate-app/tools/validate.sh "$f" \
    && echo "  OK" \
    || echo "  FAIL"
done
```

Expected: every file ends with `OK`. cwd has DEVICE-SKILL.md so it's
auto-discovered without `--device-skill`.

- [ ] **Step 2: If any app fails**

Possible causes:
- The app uses a sandbox-generic identifier that builtins.lua doesn't
  cover (e.g. a string utility we haven't stubbed). Add the stub to
  `builtins.lua` and re-run.
- The app uses a device module that DEVICE-SKILL.md doesn't list. Either
  add the module to DEVICE-SKILL.md (correct fix), or fall back to the
  permissive `_G` metatable in validate.sh (already in place).
- A genuine bug in the app — fix the app.

If multiple apps fail with shapes that suggest the deduction approach is
fundamentally too coarse, escalate: this is the "to be tested" callout in
the spec, and the follow-on is to add an opt-in `## Validation stubs` Lua
block to `DEVICE-SKILL.md`. Note in the commit message which apps
required workarounds.

- [ ] **Step 3: Commit any builtins.lua additions**

If you had to extend `builtins.lua` to make the seeded apps pass:

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/validate-app/tools/builtins.lua
git commit -m "fix(validate-app): extend builtins.lua to cover m5stick-demo apps"
```

If no changes needed, no commit — the assumption holds for v1.

---

### Task 6: Bump plugin version

**Files:**
- Modify: `~/code/resident/tools/agent-plugin/.claude-plugin/plugin.json`

- [ ] **Step 1: Read current version**

```bash
cat ~/code/resident/tools/agent-plugin/.claude-plugin/plugin.json
```

Note the current `"version"` value. (After Plan B it should be 0.3.0.)

- [ ] **Step 2: Bump to 0.4.0**

Change `"version": "0.3.0"` → `"version": "0.4.0"`.

- [ ] **Step 3: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/.claude-plugin/plugin.json
git commit -m "chore(agent-plugin): bump to 0.4.0 for validate-app"
```

---

## Self-review

- **Spec coverage:** validate-app § "Behavior" mapped to:
  - lua presence check (Task 3 step 1)
  - DEVICE-SKILL.md scan + module deduction (Task 2 + Task 3 step 1)
  - hardcoded built-in stubs (Task 1 step 3, builtins.lua)
  - permissive `_G` fallback (Task 3 step 1)
  - lifecycle assertion + 5×on_tick + ctx initialization (Task 1 step 4, harness.lua)
  - structured pass/fail with exit code (Task 1 step 4 + Task 3 step 1)
- The "assumption to test" callout (stub deduction sufficient) is its own task (Task 5).
- **Placeholders:** none — every Lua/shell file is fully written; every test fixture is concrete.
- **Type consistency:** SKILL.md exit codes (0/1/2), validate.sh exit codes, and run.sh fixture expectations all align. `validate-app: OK` and `validate-app: FAIL: <…>` strings are consistent.
