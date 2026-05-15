---
name: push-app
description: >-
  Use to push a Resident Lua app to a connected device. Accepts either a
  Lua file (push directly) or a natural-language description (chains
  through create-app + validate-app first, then pushes the result).
  Triggered by /resident:push-app or "push this app to my device".
---

# push-app

The user-facing entry point for getting Lua running on a Resident
device. Accepts two shapes of input:

1. **A Lua file (or stdin):** push directly via the relay.
2. **A natural-language description:** chain through `create-app` to
   generate the Lua, `validate-app` to check it, then push the result.

This makes the common "I have an idea, run it on the device" path one
command. For tighter control, invoke `create-app`, `validate-app`, and
the push step independently.

## Required configuration

You always need:

- **`--device-id <id>`** — the deviceId your firmware reports (read it
  off the device's screen, or check whatever your firmware persists).
  Treat as an unguessable secret — anyone with the deviceId can push to
  or read your device.
- **`--base-url <url>`** — defaults to `https://resident.inanimate.tech`
  if not provided. Override for self-hosted relays
  (e.g. `http://localhost:8787` during `wrangler dev`).

Both can be persisted across invocations:
- Env vars: `RESIDENT_DEVICE_ID`, `RESIDENT_BASE_URL`.
- Local file: `./.resident-device-id` (deviceId only).

Ask the user once per session if these aren't set; don't re-ask.

## Targeting the simulator

Device IDs that start with `sim-` target the in-browser simulator on
[resident.inanimate.tech](https://resident.inanimate.tech/#try-it-now).
The simulator emulates an M5StickC Plus2 — a bundled DEVICE-SKILL.md
for that surface ships at
`${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/m5stick/DEVICE-SKILL.md`.

When the user-supplied device ID starts with `sim-` AND no
`--device-skill` was provided AND no `./DEVICE-SKILL.md` exists in cwd,
**forward** the bundled path to `create-app` (and to `validate-app`
if you're calling it directly) as `--device-skill
${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/m5stick/DEVICE-SKILL.md`.
This lets a marketplace user with no local Resident checkout push apps
to the simulator with zero setup.

If the user has their own `./DEVICE-SKILL.md` in cwd, that takes
precedence — they presumably know what they're doing. The `sim-`
auto-forward is a convenience for the bare quick-start path.

## Workflow — file input

If the user gives a Lua file path, OR a Lua source via stdin, OR a
specific path that already exists, just push it:

```bash
"${CLAUDE_PLUGIN_ROOT}/skills/push-app/tools/push.sh" \
  --base-url https://resident.inanimate.tech \
  --device-id abc12345 \
  device-apps/foo.lua

cat device-apps/foo.lua | "${CLAUDE_PLUGIN_ROOT}/skills/push-app/tools/push.sh" \
  --base-url http://localhost:8787 \
  --device-id abc12345
```

`${CLAUDE_PLUGIN_ROOT}` is set by Claude Code to the absolute path of the
installed plugin — always use it to reference bundled tools; the CWD is
the user's project, not the skill directory.

## Workflow — natural-language description

If the user gives a description (no file, no Lua source on stdin —
just a sentence like "make the screen flash red on shake"):

1. **Generate.** Invoke `/resident:create-app` (the sibling skill) with
   the description and an `--out` path under `device-apps/<slug>.lua`.
   If the user also passed `--device-skill <path>` and/or one or more
   `--ref <path>` flags to push-app, forward them verbatim to
   create-app. If neither was passed AND the device ID starts with
   `sim-` AND there's no `./DEVICE-SKILL.md` in cwd, forward
   `--device-skill ${CLAUDE_PLUGIN_ROOT}/skills/create-app/docs/m5stick/DEVICE-SKILL.md`
   (the bundled simulator surface). That skill embeds the sandbox
   docs, resolves DEVICE-SKILL.md (caller path → cwd → prompt), reads
   any `--ref` files, and produces tight Lua source. It also chains
   through `/resident:validate-app` automatically; a returned source
   is already compile- and lifecycle-checked.

2. **Push.** Run `${CLAUDE_PLUGIN_ROOT}/skills/push-app/tools/push.sh`
   against the file produced in step 1.

3. **Show the user the Lua you generated.** They want to see it. Print
   the file path and a preview.

If create-app stops asking for a DEVICE-SKILL.md path, surface its
prompt to the user verbatim and stop — don't try to skip validation
or guess a path.

## Exit codes (for the underlying push.sh script)

- `0` — sent (HTTP 200)
- `1` — device not connected (HTTP 503)
- `2` — environment / argument error (missing flag, file not found, no `jq`)
- `3` — other HTTP error (full body printed to stderr)

## Self-hosted vs hosted

- Hosted relay: `https://resident.inanimate.tech/devices/<id>/send`
  (provided as a default for developers).
- Self-hosted: any worker that exposes the same protocol —
  `POST /devices/<id>/send` with `Content-Type: application/json` and
  body `{ "type": "app", "code": "<lua source>" }`.
- For local dev with `wrangler dev`, base-url is `http://localhost:8787`.
