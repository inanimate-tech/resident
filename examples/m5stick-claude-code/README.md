# M5Stick + Claude Code

An overlay on the [m5stick-demo](../m5stick-demo) example that turns your
M5StickC Plus2 into a Claude Code permission notifier — every time Claude Code
asks for permission to run a tool, the Worker pings the device as a Lua app
event.

Nothing on the device side changes; only the Worker code needs to be replaced.

## How it works

1. Claude Code's [`PermissionRequest` HTTP hook](https://code.claude.com/docs/en/hooks) POSTs the permission payload to the Worker.
2. The Worker (`server.ts`) accepts it at `/hook/permission-request`, extracts a one-line summary, and broadcasts `{type:"app_event", name:"permission", data:{tool, summary}}` over the existing WebSocket to the device.
3. The Worker returns `204 No Content`, so Claude Code still prompts you in the terminal — you approve or deny there as normal.
4. On the device, the currently-loaded Lua app receives the event via `on_event(ctx, e)`.

## Files in this directory

| File        | Where it goes                                | What it is                                                   |
|-------------|----------------------------------------------|--------------------------------------------------------------|
| `server.ts` | `server/src/server.ts` in your m5stick-demo  | Replacement Worker — adds the `/hook/permission-request` path |
| `cat.lua`   | send via `./send-app.sh cat.lua`             | Example Lua app for the device                               |

## Setup

Start from a working [m5stick-demo](../m5stick-demo). Then:

1. Replace `server/src/server.ts` in that project with the `server.ts` here.
2. Deploy the server (`npm run deploy` in the m5stick-demo `server/` directory).
3. Add the hook config below to your `.claude/settings.json` (see next section).
4. Optionally push `cat.lua` to the device.

## Claude Code hook config

Add to your project's `.claude/settings.json` (or `~/.claude/settings.json` for every session):

```json
{
  "hooks": {
    "PermissionRequest": [
      {
        "hooks": [
          {
            "type": "http",
            "url": "https://m5stick-demo.genmon.workers.dev/agents/device-agent/m5stick-demo/hook/permission-request"
          }
        ]
      }
    ]
  }
}
```

Swap the URL for whatever host you deployed the Worker to.

## Lua event payload

The currently-loaded Lua app receives the event via `on_event`:

```lua
function on_event(ctx, e)
  if e.name == "permission" then
    -- e.data.tool     string, e.g. "Bash" (truncated to 48 chars)
    -- e.data.summary  string, e.g. "rm -rf node_modules" (truncated to 180 chars)
  end
end
```

`data` is intentionally flat — the Outrun sandbox's event JSON parser drops nested objects and arrays, and the serialised payload must fit in a 256-byte buffer.

## Test without Claude Code

With the device connected, send a synthetic payload:

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"tool_name":"Bash","tool_input":{"command":"rm -rf node_modules"}}' \
  https://m5stick-demo.genmon.workers.dev/agents/device-agent/m5stick-demo/hook/permission-request
```

The device's currently-loaded Lua app will receive an event with `e.name == "permission"` and `e.data == { tool = "Bash", summary = "rm -rf node_modules" }`.
