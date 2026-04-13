# M5Stick Demo

A minimal example of an [Outrun](https://github.com/user/outrun) device connected to a Cloudflare Workers server. The server accepts Lua apps via HTTP POST and broadcasts them to connected M5StickC Plus2 devices over WebSocket.

## Structure

```
m5stick-demo/
├── device/          # PlatformIO firmware for M5StickC Plus2
├── server/          # Cloudflare Worker (Durable Object + web UI)
├── device-apps/     # Example Lua apps
└── send-app.sh      # CLI tool to send apps to devices
```

## Server

The server is a Cloudflare Worker with a single Durable Object (`DeviceAgent`). Each device gets its own DO instance identified by device ID.

### Setup

```bash
cd server
npm install
npm run dev
```

### Deploy

```bash
cd server
npm run deploy
```

### Web UI

Open the server URL in a browser. Enter a device ID to monitor its connection status and send Lua apps interactively.

### API

POST Lua code to a device:

```bash
curl -X POST -H "Content-Type: text/plain" \
  --data-binary @device-apps/hello.lua \
  https://your-worker.workers.dev/agents/device-agent/DEVICE_ID
```

## Device

The device firmware runs on M5StickC Plus2. It connects via WebSocket and receives Lua apps to execute.

### Available Lua APIs

- `screen.clear(r, g, b)` — clear display
- `screen.text(x, y, str)` — draw text
- `screen.fill_rect(x, y, w, h, r, g, b)` — draw filled rectangle
- `screen.pixel(x, y, r, g, b)` — draw pixel
- `screen.flip()` — flush buffer to screen
- `screen.width()`, `screen.height()` — display dimensions
- `imu.accel()` — returns ax, ay, az (g-force)
- `imu.gyro()` — returns gx, gy, gz (degrees/sec)
- `buzzer.beep(freq, duration_ms)` — play tone
- `buzzer.tone(freq)` — continuous tone
- `buzzer.stop()` — stop sound

### Lua App Callbacks

```lua
function init(ctx)        -- called once after app loads
function on_tick(ctx, dt_ms)  -- called ~10 times per second
function on_event(ctx, e)     -- called on button presses
```

### Build & Flash

```bash
cd device
pio run -t upload
```

## send-app.sh

Send a Lua app to a device from the command line:

```bash
# Production
./send-app.sh DEVICE_ID device-apps/hello.lua

# Local dev
./send-app.sh --dev-url http://localhost:8787 DEVICE_ID device-apps/hello.lua
```
