# Plan D — create-app and push-app skills Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the two remaining skills under `tools/agent-plugin/skills/`:
- `create-app` — generates Lua app source from a natural-language description, using embedded sandbox docs + the project's `DEVICE-SKILL.md`. Writes to `--out path` or stdout.
- `push-app` — POSTs a Lua app file (or stdin) to a Resident relay at `<base-url>/devices/<device-id>/send`.

**Architecture:**
- `create-app` is mostly a SKILL.md that tells Claude how to compose embedded sandbox docs + DEVICE-SKILL.md + the user's description into Lua source. The "tools" are: the embedded sandbox doc subset (a curated `.md` shipped with the skill), and a tiny `write-out.sh` helper that handles `--out`/stdout writing so the agent doesn't have to script it. Lua generation itself happens in the agent's reasoning, not a script.
- `push-app` is a thin shell script wrapping `curl` — accepts `--base-url`, `--device-id`, and a file path or stdin; wraps in `{type:"app", code:"..."}`; POSTs JSON; maps responses to clear stderr lines and exit codes.

**Tech Stack:** Bash, `curl`, `jq` (for JSON construction; `brew install jq`), Markdown.

**Spec reference:** `~/code/resident/docs/superpowers/specs/2026-05-07-resident-sandbox-tooling-design.md` § "Skills > create-app" and § "Skills > push-app".

**Working directory for this plan:** `~/code/resident/`. Tests for push-app target the `wrangler dev` server from Plan A; if Plan A isn't yet running, push-app tests against `examples/m5stick-demo`'s deployed Worker (which speaks a similar text/plain protocol — the JSON test will return 4xx, which is fine to assert). For full integration, run Plan A's `wrangler dev` first.

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `tools/agent-plugin/skills/create-app/SKILL.md` | create | Skill metadata + workflow |
| `tools/agent-plugin/skills/create-app/docs/sandbox.md` | create | Curated sandbox-API subset, embedded for the agent to read |
| `tools/agent-plugin/skills/create-app/tools/write-out.sh` | create | Helper: write to --out path or stdout |
| `tools/agent-plugin/skills/push-app/SKILL.md` | create | Skill metadata + workflow |
| `tools/agent-plugin/skills/push-app/tools/push.sh` | create | Entry-point CLI |
| `tools/agent-plugin/.claude-plugin/plugin.json` | modify | Bump to 0.5.0 |

---

### Task 1: Curate the embedded sandbox doc for create-app

The agent needs Resident's sandbox-API surface to write Lua. Start from
`~/code/resident/docs/api.md` and trim to the Lua-side subset: lifecycle,
`ctx`, `log`, `time`, shader-compatible globals, math globals, app/event
JSON shape (what gets received via `app_event`). Drop C++/driver/extension
authoring content — that's not relevant to Lua generation.

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/create-app/docs/sandbox.md`

- [ ] **Step 1: Make the directory**

```bash
mkdir -p ~/code/resident/tools/agent-plugin/skills/create-app/docs
mkdir -p ~/code/resident/tools/agent-plugin/skills/create-app/tools
```

- [ ] **Step 2: Write the curated sandbox doc**

Create `~/code/resident/tools/agent-plugin/skills/create-app/docs/sandbox.md`:

```markdown
# Resident Sandbox — Lua API (for app authors)

This is the surface every Resident-based device exposes to Lua apps. For
device-specific modules (screen, imu, buzzer, etc.), see the project's
DEVICE-SKILL.md.

## App lifecycle

An app must define at least one of these globals. If none are defined,
the device rejects the upload.

\`\`\`lua
function init(ctx)
  -- called once after compilation; one-time setup
end

function on_tick(ctx, dt_ms)
  -- called at 10 FPS; dt_ms is elapsed ms since the last tick
end

function on_event(ctx, event)
  -- called for each queued event (driver events and app_events)
end
\`\`\`

## ctx table

Every callback receives a `ctx` table with these fields:

| Field | Type | Description |
|-------|------|-------------|
| `time_ms` | integer | Milliseconds since the current app was loaded |
| `trigger_count` | integer | Number of `"button"` driver events since the last app load |
| `utc_h` | integer | Current UTC hour (0–23) |
| `utc_m` | integer | Current UTC minute (0–59) |
| `localtime_h` | integer | Local hour — equals `utc_h` unless a timezone has been set |
| `localtime_m` | integer | Local minute — equals `utc_m` unless a timezone has been set |
| `day_id` | integer | Days since boot |

Use `ctx.time_ms` for animations (counts from app load, no reset).
Use `ctx.trigger_count` for mode switching.
Use `ctx.localtime_h/m` for time-of-day effects.

## event table

`on_event(ctx, event)` receives:

| Field | Type | Description |
|-------|------|-------------|
| `event.name` | string | Event name (e.g. `"button"`, custom names from app_events) |
| `event.from` | string | Source identifier — empty for driver events |
| `event.ts_ms` | integer | Timestamp in ms when the event was queued |
| *(driver fields)* | any | Driver events flatten extra fields directly (e.g. `event.index`) |
| `event.data` | table | App events: parsed JSON `data` object as a Lua table |

\`\`\`lua
function on_event(ctx, event)
  if event.name == "button" then
    -- driver event: e.g. event.index, event.state
    log.info("button " .. tostring(event.index))
  elseif event.name == "color_change" then
    -- app event: nested data
    log.info("hue: " .. tostring(event.data.hue))
  end
end
\`\`\`

## log module

\`\`\`lua
log.info("hello")
log.warn("careful")
log.error("something broke")  -- also emits a log_error telemetry event
\`\`\`

## time module

NTP-backed wall clock; UTC unless the device has a timezone configured.

| Function | Returns |
|----------|---------|
| `time.is_valid()` | boolean — true once NTP has synced |
| `time.has_timezone()` | boolean — true if a timezone is set |
| `time.hour()` | integer 0–23 |
| `time.minute()` | integer 0–59 |
| `time.second()` | integer 0–59 |
| `time.day_id()` | integer — days since boot, useful as a daily-state cache key |

## kv module

Persistent string key/value store, ~4 KB total budget.

\`\`\`lua
local v = kv.get("count")    -- returns string or nil
kv.set("count", "42")        -- returns true on success
\`\`\`

Apps that don't need persistence can ignore `kv` — but if they call it,
the calls are safe even when the device has no storage.

## Shader-compatible globals

These functions are always in scope — designed to work both in shader
expressions and full apps.

| Function | Returns | Description |
|----------|---------|-------------|
| `rgb(r, g, b)` | integer | Pack normalized floats (0–1) into a colour (negative-int sentinel) |
| `fract(x)` | number | `x - floor(x)` |
| `beat(bpm, t)` | number | `t / (60000 / bpm)` — beat phase in beats |
| `noise2d(x, y)` | number | Deterministic 2D value noise, returns -1..+1 |

Math globals registered without the `math.` prefix:

`floor`, `ceil`, `abs`, `sin`, `cos`, `tan`, `sqrt`, `min`, `max`, `fmod`.

## App constraints

| Limit | Value |
|-------|-------|
| `on_tick` rate | 10 FPS (100 ms interval) |
| Event ring buffer | 8 events; oldest dropped when full |
| `event.data` (app_event JSON) | 256 bytes |
| Driver event field name | 32 chars |
| Runtime-error rate limit | 3 errors then cooldown of 5 s |

Apps too large for the Lua compiler crash the device. Keep apps tight —
short apps survive memory limits better than long ones.

## What's NOT in this document

This document covers the universal sandbox surface. For device-specific
modules and hardware (screen sizes, sensor units, button layouts), read
the firmware project's `DEVICE-SKILL.md`.
```

- [ ] **Step 3: Verify the file is reasonable size and renders**

```bash
wc -l ~/code/resident/tools/agent-plugin/skills/create-app/docs/sandbox.md
head -30 ~/code/resident/tools/agent-plugin/skills/create-app/docs/sandbox.md
```

Expected: under 200 lines, header + lifecycle visible.

- [ ] **Step 4: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/create-app/docs/sandbox.md
git commit -m "docs(create-app): add curated sandbox.md for embedding in skill"
```

---

### Task 2: Create create-app SKILL.md and write-out helper

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/create-app/SKILL.md`
- Create: `~/code/resident/tools/agent-plugin/skills/create-app/tools/write-out.sh`

- [ ] **Step 1: Write the SKILL.md**

Create `~/code/resident/tools/agent-plugin/skills/create-app/SKILL.md`:

```markdown
---
name: create-app
description: >-
  Use to generate a Resident Lua app from a natural-language description.
  Reads embedded sandbox docs and the firmware project's DEVICE-SKILL.md
  to know the runtime surface, then writes Lua source to --out or stdout.
  Triggered by /resident:create-app or "write a Lua app for this device".
---

# create-app

Generate a Resident Lua app from a description. Output is plain Lua
source — no validation, no push. The agent composes those steps via the
sibling `validate-app` and `push-app` skills.

## What you need

1. **A description** — one or more sentences from the user about what the
   app should do.
2. **DEVICE-SKILL.md at the firmware project root (cwd)** — describes
   the device-specific Lua module surface. If missing, stop and tell the
   user to run `/resident:write-device-skill` first.
3. **The embedded sandbox docs** — read `docs/sandbox.md` (this skill's
   directory). Covers lifecycle, ctx, log, time, kv, shader globals,
   math globals, constraints.

## Workflow

1. Read `docs/sandbox.md`.
2. Read `./DEVICE-SKILL.md`. If absent → tell the user:
   > "I need a DEVICE-SKILL.md at the firmware project root. Invoke
   > /resident:write-device-skill to author one, then re-run me."
   > Exit without writing anything.
3. Read the user's description.
4. Compose Lua source that:
   - Defines `init`, `on_tick`, and/or `on_event` as appropriate.
   - Uses only the modules and functions shown in DEVICE-SKILL.md and
     `docs/sandbox.md`. Don't invent APIs.
   - Stays small — keep apps tight; long apps risk hitting memory limits.
   - Follows DEVICE-SKILL.md's "Practical Tips" section if present
     (e.g. `screen.flip()` MUST after every draw).
5. Write the result via `tools/write-out.sh`:
   - With `--out path/to/app.lua` → writes the file. Tell the user the path.
   - Without `--out` → prints to stdout.

## Composition

`create-app` does not call `validate-app` or `push-app`. The agent is
expected to compose them after generation:

1. `create-app --out device-apps/foo.lua "<description>"`
2. `validate-app device-apps/foo.lua` — if fails, re-prompt yourself
   (the agent) with the error and regenerate.
3. `push-app --base-url <url> --device-id <id> device-apps/foo.lua`

## Output conventions

- The conventional location for app files in a Resident firmware project
  is `device-apps/<name>.lua`. Suggest this when asking the user for
  an `--out` path; don't create the directory unless the user agrees.
- The user's description is short — DON'T inflate it with comments or
  multi-line docstrings in the generated Lua. Keep generated code tight.
- The user can iterate. If they ask "make it slower / red instead of
  green / etc.", regenerate with the changed brief.
```

- [ ] **Step 2: Write the write-out.sh helper**

Create `~/code/resident/tools/agent-plugin/skills/create-app/tools/write-out.sh`:

```bash
#!/usr/bin/env bash
#
# write-out.sh [--out PATH] < lua_source
#
# Reads Lua source from stdin and either writes it to PATH (if --out
# given) or echoes it to stdout. Used by create-app so the agent doesn't
# inline file-writing logic.

set -euo pipefail

out=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    -h|--help)
      echo "Usage: $0 [--out PATH] < lua_source" >&2
      exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

if [[ -t 0 ]]; then
  echo "write-out.sh: error: no input on stdin" >&2
  exit 2
fi

if [[ -n "$out" ]]; then
  mkdir -p "$(dirname "$out")"
  cat > "$out"
  echo "Wrote $out" >&2
else
  cat
fi
```

- [ ] **Step 3: Make it executable**

```bash
chmod +x ~/code/resident/tools/agent-plugin/skills/create-app/tools/write-out.sh
```

- [ ] **Step 4: Smoke-test the helper**

```bash
echo 'function init(ctx) end' | \
  ~/code/resident/tools/agent-plugin/skills/create-app/tools/write-out.sh
```

Expected stdout: `function init(ctx) end`.

```bash
tmp=$(mktemp -d)
echo 'function init(ctx) end' | \
  ~/code/resident/tools/agent-plugin/skills/create-app/tools/write-out.sh \
    --out "$tmp/sub/app.lua"
cat "$tmp/sub/app.lua"
```

Expected stderr: `Wrote .../sub/app.lua`. The file exists with the
expected content. Cleanup: `rm -rf "$tmp"`.

- [ ] **Step 5: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/create-app/
git commit -m "feat(agent-plugin): add create-app skill (SKILL.md + write-out helper)"
```

---

### Task 3: Create push-app skill

**Files:**
- Create: `~/code/resident/tools/agent-plugin/skills/push-app/SKILL.md`
- Create: `~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh`

- [ ] **Step 1: Make the directory**

```bash
mkdir -p ~/code/resident/tools/agent-plugin/skills/push-app/tools
```

- [ ] **Step 2: Write the SKILL.md**

Create `~/code/resident/tools/agent-plugin/skills/push-app/SKILL.md`:

```markdown
---
name: push-app
description: >-
  Use to push a Resident Lua app to a connected device via the relay.
  Required flags: --base-url and --device-id. Reads the app from a
  positional file argument or stdin. Triggered by /resident:push-app or
  "push this app to my device".
---

# push-app

Send a Lua app to a Resident relay's `POST /devices/<id>/send` endpoint.
The relay forwards the JSON verbatim to the device's WebSocket.

## What you need

- **`--base-url <url>`** — required. e.g. `https://resident.inanimate.tech`,
  `http://localhost:8787` for local dev. Probe with `curl -i $base/devices/test/send -X POST` if unsure.
- **`--device-id <id>`** — required. Treat as an unguessable secret —
  anyone holding the deviceId can push to or connect as that device.
- **The Lua app** — pass as a positional file argument, or pipe via stdin.

## Usage

```bash
./tools/push.sh \
  --base-url https://resident.inanimate.tech \
  --device-id abc123…  \
  device-apps/foo.lua

cat device-apps/foo.lua | ./tools/push.sh \
  --base-url http://localhost:8787 \
  --device-id test-1234
```

## Exit codes

- `0` — sent (HTTP 200)
- `1` — device not connected (HTTP 503)
- `2` — environment / argument error (missing flag, file not found, no `jq`)
- `3` — other HTTP error (full body printed to stderr)

## Composition

`push-app` does not validate. Run `validate-app` first if you want a
pre-flight check; otherwise the device will receive whatever you send and
report errors via telemetry (which v1 does not surface back to the skill).

## Self-hosted vs hosted

- Hosted relay: `https://resident.inanimate.tech/devices/<id>/send`
  (deployed by `resident-web` — see Plan A).
- Self-hosted: any worker that exposes the same protocol —
  `POST /devices/<id>/send` with `Content-Type: application/json` and
  body `{ "type": "app", "code": "<lua source>" }`.
- For local dev with `wrangler dev`, base-url is `http://localhost:8787`.
```

- [ ] **Step 3: Write push.sh**

Create `~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh`:

```bash
#!/usr/bin/env bash
#
# push.sh — POST a Lua app to a Resident relay.
#
# Usage:
#   push.sh --base-url <url> --device-id <id> [APP_FILE]
#   cat app.lua | push.sh --base-url <url> --device-id <id>

set -euo pipefail

base_url=""
device_id=""
app_file=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --base-url)  base_url="$2";  shift 2 ;;
    --device-id) device_id="$2"; shift 2 ;;
    -h|--help)
      cat <<EOF
Usage: $0 --base-url <url> --device-id <id> [APP_FILE]
       cat app.lua | $0 --base-url <url> --device-id <id>
EOF
      exit 0 ;;
    -*) echo "Unknown option: $1" >&2; exit 2 ;;
    *)  app_file="$1"; shift ;;
  esac
done

if [[ -z "$base_url" ]]; then
  echo "push-app: error: --base-url is required" >&2; exit 2
fi
if [[ -z "$device_id" ]]; then
  echo "push-app: error: --device-id is required" >&2; exit 2
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "push-app: error: 'jq' not found. Install with: brew install jq" >&2
  exit 2
fi

# Read source from file or stdin.
if [[ -n "$app_file" ]]; then
  if [[ ! -f "$app_file" ]]; then
    echo "push-app: error: file not found: $app_file" >&2; exit 2
  fi
  code=$(cat "$app_file")
elif [[ ! -t 0 ]]; then
  code=$(cat)
else
  echo "push-app: error: no app provided. Pass a file path or pipe via stdin." >&2
  exit 2
fi

if [[ -z "$code" ]]; then
  echo "push-app: error: app source is empty" >&2; exit 2
fi

# Build the JSON envelope.
payload=$(jq -n --arg code "$code" '{type: "app", code: $code}')

endpoint="${base_url}/devices/${device_id}/send"

# POST and capture both body and status.
tmp_body=$(mktemp)
trap 'rm -f "$tmp_body"' EXIT

http_code=$(curl -sS -o "$tmp_body" -w "%{http_code}" \
  -X POST \
  -H "Content-Type: application/json" \
  --data-binary "$payload" \
  "$endpoint")

case "$http_code" in
  200)
    echo "push-app: sent to $endpoint" >&2
    exit 0
    ;;
  503)
    echo "push-app: device not connected ($endpoint)" >&2
    exit 1
    ;;
  400|415|404)
    echo "push-app: error: HTTP $http_code from $endpoint" >&2
    cat "$tmp_body" >&2
    exit 3
    ;;
  *)
    echo "push-app: error: HTTP $http_code from $endpoint" >&2
    cat "$tmp_body" >&2
    exit 3
    ;;
esac
```

- [ ] **Step 4: Make it executable**

```bash
chmod +x ~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh
```

- [ ] **Step 5: Test argument validation (no server needed)**

```bash
~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh
```

Expected stderr: `--base-url is required`. Exit 2.

```bash
~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh \
  --base-url http://localhost:8787
```

Expected stderr: `--device-id is required`. Exit 2.

```bash
~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh \
  --base-url http://localhost:8787 --device-id test
```

Expected stderr: `no app provided`. Exit 2.

- [ ] **Step 6: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/skills/push-app/
git commit -m "feat(agent-plugin): add push-app skill (SKILL.md + push.sh)"
```

---

### Task 4: Integration test push-app against wrangler dev

**Prerequisite:** Plan A is implemented in `~/code/resident-web/` and you
can run `wrangler dev` there. If Plan A isn't done yet, skip this task
and revisit after Plan A lands.

**Files:** none modified.

- [ ] **Step 1: Start wrangler dev**

In one terminal:
```bash
cd ~/code/resident-web && npx wrangler dev
```

Wait for `http://localhost:8787` to be ready.

- [ ] **Step 2: Test 503 with no device connected**

In another terminal:
```bash
echo 'function init(ctx) end' | \
  ~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh \
    --base-url http://localhost:8787 \
    --device-id test-1234
```

Expected stderr: `device not connected`. Exit 1.

- [ ] **Step 3: Test 200 with a connected websocat**

In a third terminal: `websocat ws://localhost:8787/devices/test-1234`.
Wait until it's connected (no error).

In the second terminal, re-run the push:
```bash
echo 'function init(ctx) end' | \
  ~/code/resident/tools/agent-plugin/skills/push-app/tools/push.sh \
    --base-url http://localhost:8787 \
    --device-id test-1234
```

Expected stderr: `sent to http://localhost:8787/devices/test-1234/send`.
Exit 0.
Expected websocat output: `{"type":"app","code":"function init(ctx) end\n"}`.

(If the trailing `\n` is missing in the JSON-escaped form, that's fine —
jq might or might not preserve it depending on how the source ends.)

- [ ] **Step 4: Tear down**

Stop `websocat` and `wrangler dev`. No commit — manual integration check.

---

### Task 5: Bump plugin version

**Files:**
- Modify: `~/code/resident/tools/agent-plugin/.claude-plugin/plugin.json`

- [ ] **Step 1: Read current version**

```bash
cat ~/code/resident/tools/agent-plugin/.claude-plugin/plugin.json
```

After Plan C this should be 0.4.0.

- [ ] **Step 2: Bump to 0.5.0**

Change `"version": "0.4.0"` → `"version": "0.5.0"`.

- [ ] **Step 3: Commit**

```bash
cd ~/code/resident
git add tools/agent-plugin/.claude-plugin/plugin.json
git commit -m "chore(agent-plugin): bump to 0.5.0 for create-app + push-app"
```

---

### Task 6: Update examples/m5stick-demo/README.md with deviceId guidance

**Files:**
- Modify: `~/code/resident/examples/m5stick-demo/README.md`

The spec calls for the deviceId-as-auth note in the m5stick example's
README. The seed example uses `m5stick-demo` (guessable, fine for self-
hosted demo); a production-style deployment should use a long random ID.

- [ ] **Step 1: Read the current README**

```bash
cat ~/code/resident/examples/m5stick-demo/README.md
```

Note the current structure so the addition is consistent.

- [ ] **Step 2: Add a "Device IDs as auth" section near the top**

Append to the README (or insert under whatever "Quick start" / "Setup"
section exists), preserving the existing content:

```markdown
## Device IDs as auth

This example uses the deviceId `m5stick-demo` — guessable on purpose,
because it's a public demo. **For a real deployment**, generate a long
random ID (≥ 128 bits of entropy, e.g. UUIDv4 or 32 hex chars) and treat
it like an API key:

- Anyone with the deviceId can push apps to or connect as that device.
- The default Resident relay (`resident.inanimate.tech`) does no other
  authentication in v1.
- Self-hosted relays inherit the same model unless you add auth on top.
```

- [ ] **Step 3: Commit**

```bash
cd ~/code/resident
git add examples/m5stick-demo/README.md
git commit -m "docs(m5stick-demo): document deviceId-as-auth model"
```

---

## Self-review

- **Spec coverage:**
  - create-app SKILL.md (Task 2) covers: embedded sandbox docs, DEVICE-SKILL.md read, --out path or stdout, "no validation, no push" composition contract.
  - The curated `docs/sandbox.md` (Task 1) covers everything listed in spec § create-app "embedded sandbox docs": lifecycle, ctx, log, time, kv, shader globals, math globals.
  - push-app SKILL.md + push.sh (Task 3) cover: required `--base-url` and `--device-id` flags, file-or-stdin input, JSON envelope `{type:"app", code:"..."}`, response mapping (200/503/4xx).
  - Integration test (Task 4) exercises Task 3 end-to-end against Plan A.
  - README update (Task 6) covers spec § "Auth model" wording about deviceIds.
- **Placeholders:** none — every step shows code or exact commands; the README addition is a complete paragraph.
- **Type consistency:** push.sh exit codes (0/1/2/3) match the SKILL.md "Exit codes" section; the JSON envelope shape `{type:"app", code:...}` matches Plan A's relay validation; the wire format and endpoint `/devices/<id>/send` match the spec and Plan A's DeviceAgent.handleSend.
