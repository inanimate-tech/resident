# M5Stick Demo

A working example of an M5StickC Plus2 (or M5StickS3) running the [Resident](https://github.com/inanimate-tech/resident) Lua sandbox. The device opens a WebSocket to the canonical Resident relay at [resident.inanimate.tech](https://resident.inanimate.tech) and waits for apps and events to be pushed to it.

The example is the on-ramp: **flash the device, try it against the public relay, push some Lua at it.** If you want to build your own back-end server, start from [`examples/server-template/`](../server-template/) — that's the minimum Cloudflare Worker running the same relay.

## Structure

```
m5stick-demo/
├── device/          # PlatformIO firmware for M5StickC Plus2 / M5StickS3
├── device-apps/     # Example Lua apps for the sandbox
├── send-app.sh      # CLI tool to push apps without installing the agent plugin
└── DEVICE-SKILL.md  # Lua-side device surface (used by /resident:create-app etc.)
```

---

## 1. Flash the device

[Install the PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html). Then connect your M5StickC Plus2 over USB:

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

> **Heads up.** `resident.inanimate.tech` is a public relay with no authentication beyond the deviceId itself. Anyone who knows your deviceId can push apps to your device. Fine for hacking; for anything more permanent, run your own server.

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

The `device-apps/` directory has sample apps: `hello.lua`, `bounce.lua`, `accel.lua`, `rainbow.lua`, and more.

If you've installed the [resident agent plugin](https://github.com/inanimate-tech/agent-plugins), the `/resident:push-app` skill does the same thing from inside Claude Code.

### What's happening under the hood

`send-app.sh` does this:

```bash
curl -X POST https://resident.inanimate.tech/devices/abc12345/send \
  -H 'Content-Type: application/json' \
  -d '{"type":"app","code":"<your lua source>"}'
```

The relay validates the JSON shape, then forwards it verbatim to the device's WebSocket. The Resident sandbox on the device parses the message, compiles the Lua, and runs it. Any previously-loaded app is stopped first.

`type: "shader"` and `type: "app_event"` work the same way — the relay forwards any well-formed JSON object; it never inspects the contents.

---

## 3. Run your own server

The relay is a Cloudflare Worker built on top of [`@inanimate/resident`](https://www.npmjs.com/package/@inanimate/resident). To self-host or customise, fork [`examples/server-template/`](../server-template/) — that's the bare reference. After you've deployed it, change `RESIDENT_HOST` in `device/src/main.cpp` to your worker URL and `pio run -t upload` again.

For an example that customises the server (a `/register` endpoint that pushes per-device config to the firmware on boot), see [`examples/m5stick-clock/`](../m5stick-clock/).

---

## Lua API on this device

See [`DEVICE-SKILL.md`](./DEVICE-SKILL.md) for the complete Lua-side surface (screen, IMU, buzzer, buttons). The `/resident:create-app` skill reads this file to write apps for the device.

---

## Device IDs as auth

The deviceId is the *only* secret. Anyone who knows it can push apps to or read your device. The example uses an 8-character hex string derived from the chip's MAC address — fine for development and the public demo, but **for anything you actually deploy, switch to a longer random ID** (≥ 128 bits of entropy, e.g. UUIDv4 or 32 hex chars) and treat it like an API key.

To do that today, replace the call to `getDeviceId()` in `device/src/main.cpp` (`onTransportsWillConnect`) with a constant or NVS-backed value, and use the same value in `send-app.sh`.
