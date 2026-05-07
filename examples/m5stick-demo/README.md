# M5Stick Demo

A working example of an M5StickC Plus2 running the [Resident](https://github.com/inanimate-tech/resident) Lua sandbox, receiving apps over WebSocket from the canonical Resident relay at [resident.inanimate.tech](https://resident.inanimate.tech) — or from a self-hosted Cloudflare Worker bundled in `server/`.

The example is the on-ramp: **flash the device, try it against the public relay, then optionally deploy the bundled server as the starting point for your own back-end.**

## Structure

```
m5stick-demo/
├── device/          # PlatformIO firmware for M5StickC Plus2
├── device-apps/     # Example Lua apps for the sandbox
├── send-app.sh      # CLI tool to push apps without installing the agent plugin
├── server/          # Optional self-hosted Cloudflare Worker (Durable Object + UI)
└── DEVICE-SKILL.md  # Lua-side device surface (used by /resident:create-app etc.)
```

---

## 1. Flash the device

First [install the PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html). Then connect your M5StickC Plus2 over USB:

```bash
cd device
pio run -t upload
```

For an M5StickS3:

```bash
cd device
pio run -e m5sticks3 -t upload
```

The first time the device boots, it creates a Wi-Fi access point named **Resident Stick XXXX**. Connect to it and use the captive portal to give the device your local Wi-Fi credentials. (ESP32 only does 2.4 GHz.)

Once connected to Wi-Fi, the device opens a WebSocket to `wss://resident.inanimate.tech/devices/<deviceId>` and displays its 8-character **device ID** on screen — something like `abc12345`. Note it down; you'll need it to push apps.

The deviceId is derived from the chip's MAC address. It's stable across reboots.

> **Heads up.** `resident.inanimate.tech` is a public relay with no authentication beyond the deviceId itself. Anyone who knows your deviceId can push apps to your device. That's fine for hacking, but for anything more permanent, deploy your own server (see step 3).

---

## 2. Push an app

You don't need to install anything — `send-app.sh` is a small bash script that requires only `curl` and `jq` (`brew install jq`).

```bash
# Read the device ID off the device's screen, then:
./send-app.sh --device-id abc12345 device-apps/hello.lua

# Or pipe Lua source via stdin:
echo 'function init(ctx) screen.clear(255,0,0) screen.flip() end' | \
  ./send-app.sh --device-id abc12345

# To avoid retyping the deviceId, save it once:
echo 'abc12345' > .resident-device-id
./send-app.sh device-apps/bounce.lua
```

The `device-apps/` directory has a few sample apps to try: `hello.lua`, `bounce.lua`, `accel.lua`, `rainbow.lua`.

If you've installed the [resident agent plugin](https://github.com/inanimate-tech/agent-plugins), the `/resident:push-app` skill does the same thing from inside Claude Code.

### What's actually happening

`send-app.sh` does this:

```bash
curl -X POST https://resident.inanimate.tech/devices/abc12345/send \
  -H 'Content-Type: application/json' \
  -d '{"type":"app","code":"<your lua source>"}'
```

The relay validates the JSON shape, then forwards it verbatim to the device's WebSocket. The Resident sandbox on the device parses the message, compiles the Lua, and runs it. Any previously-loaded app is stopped first.

`type: "shader"` and `type: "app_event"` work the same way — the relay forwards any well-formed JSON object; it never inspects the contents.

---

## 3. Run your own server (optional)

The bundled `server/` is a Cloudflare Worker that mirrors the canonical relay protocol. Use it as a starting point for your own back-end — for example, to add authentication, persistence, or a richer admin UI than the basic React monitor included here.

### Setup

```bash
cd server
npm install
npm run dev
```

Open http://localhost:5173 in a browser. Enter the device ID and you can push apps from a textarea, with live status of whether the device is connected.

### Deploy

```bash
cd server
npm run deploy
```

Wrangler will deploy to your own Cloudflare account. Once deployed, point the device at it: edit `device/src/main.cpp`, change `RESIDENT_HOST` to your worker's URL, and `pio run -t upload` again.

```cpp
static constexpr const char* RESIDENT_HOST = "your-worker.example.workers.dev";
```

Then push from the CLI with the same `send-app.sh`, just with `--base-url`:

```bash
./send-app.sh \
  --base-url https://your-worker.example.workers.dev \
  --device-id abc12345 \
  device-apps/hello.lua
```

### How the server is built

`server/src/server.ts` is a single Durable Object — `DeviceAgent` — extending `Agent` from the Cloudflare Agents SDK. The Worker routes `/devices/<id>/*` directly into the DO via `getAgentByName`. The DO:

- Accepts a device WebSocket on `/devices/<id>` (untagged → `device` tag).
- Accepts a monitor WebSocket on `/devices/<id>?monitor=1` (used by the bundled UI; an extension on top of the canonical protocol).
- Handles `POST /devices/<id>/send`: validates JSON shape, forwards verbatim to the device WS, echoes to monitors.
- Handles `GET /devices/<id>`: returns the deviceId and current connection count as plain text.

The relay never reads `code`, `expr`, or `name` fields — the protocol is defined by the firmware library and the skill, not the relay.

### Why the relay is dumb

This is the **end-to-end principle**: the relay forwards bytes; the firmware library and the skill agree on the JSON shape. New protocol message types (e.g. `app_event` for two-way coordination) work without redeploying the worker. See the [resident sandbox tooling spec](../../docs/superpowers/specs/2026-05-07-resident-sandbox-tooling-design.md) for the full design.

---

## Lua API on this device

See [`DEVICE-SKILL.md`](./DEVICE-SKILL.md) for the complete Lua-side surface (screen, IMU, buzzer, buttons). The `/resident:create-app` skill reads this file to write apps for the device.

---

## Device IDs as auth

The deviceId is the *only* secret. Anyone who knows it can push apps to or read your device. The example uses an 8-character hex string derived from the chip's MAC address — fine for development and for the public demo, but **for anything you actually deploy, switch to a longer random ID** (≥ 128 bits of entropy, e.g. UUIDv4 or 32 hex chars) and treat it like an API key.

To do that today, replace the call to `getDeviceId()` in `device/src/main.cpp` (`onTransportsWillConnect`) with a constant or NVS-backed value, and use the same value in `send-app.sh`.
