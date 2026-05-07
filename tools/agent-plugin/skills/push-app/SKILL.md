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
  (deployed by `resident-web`).
- Self-hosted: any worker that exposes the same protocol —
  `POST /devices/<id>/send` with `Content-Type: application/json` and
  body `{ "type": "app", "code": "<lua source>" }`.
- For local dev with `wrangler dev`, base-url is `http://localhost:8787`.
