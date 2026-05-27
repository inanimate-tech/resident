# m5stick-clock

Physical devices can get the time. It is set by [Courier](https://github.com/inanimate-tech/courier) and kept up to date using NTP.

But local time is harder as the device doesn't know its timezone. But the server can discover the device timezone via request geolocation, and share it with the device.

This example demonstrates two things on top of `m5stick-demo` and `server-template`:

1. **Server-supplied timezone.** The device fetches its timezone from the server on first connection and applies it via `Sandbox::setTimezone`, so Resident apps see local time in `ctx.localtime_h/m`.
2. **A registration step using a custom server.** The device POSTs to `/devices/<id>/register` on boot; the server returns a JSON config blob; the device applies what it cares about.

This is an overlay project, not a standalone. It's three files (plus the clock app) that overlay on top of `m5stick-demo` and `server-template`.

## What's in this directory

| File | Drops in as |
| --- | --- |
| `main.cpp` | `examples/m5stick-demo/device/src/main.cpp` |
| `worker.ts` | `examples/server-template/src/worker.ts` |
| `device-apps/swiss-clock.lua` | The app you push once both ends are running |

## Usage

### 1. Custom server

Copy this directory's `worker.ts` into the server template and deploy:

```bash
cp examples/m5stick-clock/worker.ts examples/server-template/src/worker.ts
cd examples/server-template
npm install
npx wrangler deploy
```

Wrangler prints a URL like `resident-server-template.<your-cf-account>.workers.dev` — that's your custom server.

The `worker.ts` here subclasses `DeviceAgent` and adds one route: `POST /devices/<id>/register` returns `{ "timezone": "..." }`, derived from Cloudflare's edge geolocation (`request.cf.timezone`). The relay functionality is preserved via `super.onRequest(request)`.

### 2. Firmware

Copy this directory's `main.cpp` over the m5stick-demo's, edit `RESIDENT_HOST` to your custom server URL, and flash:

```bash
cp examples/m5stick-clock/main.cpp examples/m5stick-demo/device/src/main.cpp
# edit examples/m5stick-demo/device/src/main.cpp — set RESIDENT_HOST
cd examples/m5stick-demo/device
pio run -t upload                  # M5StickC Plus2
# or: pio run -e m5sticks3 -t upload   # M5StickS3
```

`RESIDENT_HOST` is a constant near the top of `main.cpp`:

```cpp
static constexpr const char* RESIDENT_HOST = "resident-server-template.<your-cf-account>.workers.dev";
```

Watch the serial monitor on the next boot. You should see something like:

```
[register] timezone: Europe/London
```

before the WebSocket connects.

### 3. Push the clock

Once the device shows its **Device ID** screen — meaning it registered, picked up its timezone, and the WebSocket is up — push the Swiss railway-style clock:

```bash
curl -X POST https://<your-worker>/devices/<id>/send \
  -H 'Content-Type: application/json' \
  -d "{\"type\":\"app\",\"code\":$(jq -Rs . < device-apps/swiss-clock.lua)}"
```

Or with the Claude skill:

```
/resident:push-app --base-url https://<your-worker> --device-id <id> device-apps/swiss-clock.lua
```

The clock face shows local time using the geolocated timezone provided by the server.

## How registration works

### `main.cpp`

The only new code on top of the m5stick-demo `main.cpp` is `registerWithServer()` and a call to it from `onTransportsWillConnect`:

```cpp
sandbox.onTransportsWillConnect([]() {
    registerWithServer();           // POST /register → setTimezone
    String wsPath = String("/devices/") + sandbox.getDeviceId();
    sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
});
```

`registerWithServer()` is a plain `HTTPClient` POST. The response is parsed with ArduinoJson and the `timezone` field — if present — is applied via `sandbox.setTimezone(...)`. The example uses `WiFiClientSecure::setInsecure()` so you don't have to pin a CA cert; you'd want to do that for production.

### `worker.ts`

Subclasses `DeviceAgent` and overrides `onRequest`:

```ts
class ClockAgent extends DeviceAgent {
  async onRequest(request: Request): Promise<Response> {
    const url = new URL(request.url)
    if (url.pathname.endsWith("/register") && request.method === "POST") {
      const timezone =
        (request.cf as { timezone?: string })?.timezone ?? "Etc/UTC"
      return Response.json({ timezone })
    }
    return super.onRequest(request)
  }
}

// Wrangler still binds the class named "DeviceAgent" — no wrangler.jsonc edit.
export { ClockAgent as DeviceAgent }
```

`super.onRequest(request)` keeps the canonical `/send` relay working, so you can still push Lua apps the standard way.

### `device-apps/swiss-clock.lua`

A Mondaine-style Swiss railway clock — white face, 12 bar markers, black hour and minute hands, a red lollipop second hand. Reads `ctx.localtime_h/m` (which respects the server-supplied timezone) and `time.second()` for the seconds hand.
