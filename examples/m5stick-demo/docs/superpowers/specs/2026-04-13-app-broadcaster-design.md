# M5Stick Demo: App Broadcaster Server

## Overview

Simplify the m5stick-demo server from an AI chat app into a minimal "app broadcaster" — a Cloudflare Durable Object that accepts WebSocket connections from devices and lets users POST Lua apps to them. Reorganize the example directory to separate device firmware from the server.

## Directory Structure

```
examples/m5stick-demo/
├── README.md                 # Project overview, setup instructions
├── device/                   # Moved from current top-level src/, lib/, platformio.ini
│   ├── platformio.ini
│   ├── src/main.cpp
│   └── lib/drivers/...
├── server/                   # Simplified Cloudflare Worker
│   ├── src/
│   │   ├── server.ts         # DeviceAgent DO + default export
│   │   ├── app.tsx           # Single-page web UI
│   │   ├── client.tsx        # React root mount
│   │   └── styles.css        # Tailwind
│   ├── index.html
│   ├── wrangler.jsonc
│   ├── package.json
│   └── ...
├── device-apps/              # Example Lua apps
│   ├── hello.lua
│   ├── rainbow.lua
│   ├── bounce.lua
│   └── accel.lua
└── send-app.sh               # CLI tool to POST an app to production
```

## Durable Object: DeviceAgent

Extends `Agent` from the `agents` SDK (not `AIChatAgent` — no AI needed).

### WebSocket Connections

Two connection types, distinguished by query parameter:

- **Device connections**: connect to `/agents/device-agent/{deviceId}` — these are physical M5Stick devices
- **Monitor connections**: connect to `/agents/device-agent/{deviceId}?monitor=1` — these are web UI clients observing the device

On connect/close, broadcast device presence state to all monitor connections:
```json
{ "type": "status", "deviceConnected": true }
```

`deviceConnected` is true when at least one non-monitor WebSocket is open.

### POST Endpoint

`onRequest` handles POST requests. Body is raw Lua code (text/plain).

The handler:
1. Reads the request body as text
2. Wraps it as `{ type: "app", code: "<lua>" }`
3. Broadcasts the JSON message to all device (non-monitor) connections
4. Broadcasts `{ type: "app_sent", code: "<lua>" }` to all monitor connections
5. Returns 200 with `{ ok: true }`

### No State Persistence

No SQL, no stored messages, no history. The DO is purely a live relay.

## Web UI

Single page with these elements:

### Device ID Input
- Text input field for the device ID
- On change, the `useAgent` hook connects to `/agents/device-agent/{deviceId}?monitor=1`

### Connection Status
- Displays a green checkmark or red cross next to the device ID input
- Driven by `status` messages from the monitor WebSocket

### Code Textarea
- Large textarea for pasting Lua app code
- Submit button that POSTs the textarea content to `/agents/device-agent/{deviceId}`

### Last Sent
- Shows the most recently sent app code (from `app_sent` monitor messages)

### Styling
- Keep Tailwind CSS
- Drop Kumo design system, Streamdown, and all chat-related UI dependencies
- Simple, functional layout

## Device Firmware Changes

Move existing files into `device/` subdirectory:
- `src/main.cpp` -> `device/src/main.cpp`
- `lib/` -> `device/lib/`
- `platformio.ini` -> `device/platformio.ini`

The firmware code itself is unchanged. The device will need its WebSocket path updated to `/agents/device-agent/{deviceId}` — but this is configured in the Outrun library's Device class, so the main.cpp may need a host/path update to point at this new server.

## Example Lua Apps (`device-apps/`)

### hello.lua
Simple text display — shows "Hello World" on screen.

### rainbow.lua
Animated color cycling across the display using `on_tick`.

### bounce.lua
A bouncing ball animation using screen drawing primitives.

### accel.lua
Reads accelerometer data via `imu.accel()` and visualizes it on screen.

## send-app.sh

```bash
#!/bin/bash
# Usage: ./send-app.sh <device-id> <app-file.lua>
# Posts a Lua app file to a specific device via the production server
```

- Takes device ID and a Lua filename as arguments
- Resolves the production URL from `wrangler.jsonc` worker name (format: `https://{name}.{subdomain}.workers.dev`)
- Uses `npx wrangler deployments list --json` or parses the worker name to construct the URL
- POSTs the file content as text/plain to `https://{url}/agents/device-agent/{deviceId}`
- Prints success/failure

## Dependencies to Remove

From `package.json`, remove:
- `ai`, `workers-ai-provider`, `@cloudflare/ai-chat` (AI)
- `@cloudflare/kumo` (design system)
- `streamdown`, `@streamdown/code` (markdown rendering)
- `@phosphor-icons/react` (icons)
- `zod` (validation)

From `wrangler.jsonc`, remove:
- `ai` binding
- `oauth` route handling

Keep:
- `agents` SDK
- `react`, `react-dom`
- `tailwindcss`, `@tailwindcss/vite`
- Build tooling (vite, cloudflare plugin, wrangler, typescript)

## Migrations

Update `wrangler.jsonc` migrations:
- Rename class from `ChatAgent` to `DeviceAgent`
- Add a `renamed_classes` migration entry (or `delete_classes` + `new_sqlite_classes` if no data to preserve)
