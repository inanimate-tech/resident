# M5Stick Demo

A minimal example of an M5Stick with an [Outrun](https://github.com/inanimate-tech/outrun) sandbox, receiving apps from a Cloudflare Workers server.

The device connects to the server via WebSocket managed by [Courier](https://github.com/inanimate-tech/courier).

The server accepts Lua apps via HTTP POST and relays them to connected devices.

## Structure

```
m5stick-demo/
├── device/          # PlatformIO firmware for M5StickC Plus2
├── server/          # Cloudflare Worker (Durable Object + web UI)
├── device-apps/     # Example Lua apps for the sandbox
└── send-app.sh      # CLI tool to send apps to devices
```

## Device

The device firmware runs on M5StickC Plus2. It connects via WebSocket and receives Lua apps to execute in its Outrun sandbox.

### Lua App Callbacks

The Outrun sandbox provides three main callbacks for Lua apps:

```lua
function init(ctx)        -- called once after app loads
function on_tick(ctx, dt_ms)  -- called ~10 times per second
function on_event(ctx, e)     -- called on button presses
```

### Drivers and Available Lua APIs

This M5Stick project includes drivers for the display, IMU, and buzzer. An Outrun driver manages a hardware peripheral and exposes an API to Lua apps in the sandbox.

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

### Build & Flash

First [install the PlatformIO CLI](https://docs.platformio.org/en/stable/core/installation/index.html). Then connect your M5Stick, and build and flash the firmware:

```bash
cd device
pio run -t upload
```

If you're using an **M5StickS3**, specify the environment:

```bash
cd device
pio run -e m5sticks3 -t upload
```

### Connect to Wi-Fi

The device manages its own connectivity.

If it cannot connect to a configured Wi-Fi network, it creates an access point called **Outrun Stick XXXX**.

Connect to this and configure your local Wi-Fi credentials via the captive portal (note that ESP32 does not support 5GHz networks).

The device screen will show "Connecting..." until the server is deployed.

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
  https://your-worker.workers.dev/agents/device-agent/m5stick-demo
```


## send-app.sh

Send a Lua app to a device from the command line:

```bash
# From a file
./send-app.sh device-apps/hello.lua

# From stdin
echo 'function init(ctx) screen.clear(255,0,0) screen.flip() end' | ./send-app.sh

# Local dev
./send-app.sh --dev device-apps/hello.lua
```
