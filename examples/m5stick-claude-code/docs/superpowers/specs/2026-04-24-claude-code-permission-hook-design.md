# Claude Code Permission Hook → M5Stick App Event

## Overview

Add an HTTP endpoint to the `DeviceAgent` Durable Object that Claude Code's
`PermissionRequest` hook can POST to. The endpoint translates the hook payload
into a Lua sandbox `app_event` and broadcasts it over the device WebSocket.
The endpoint always returns `204 No Content`, so Claude Code never blocks —
the user still approves or denies the permission in their terminal as
normal.

No device firmware changes. No local hook script. The hook is a pure HTTP
declaration in `settings.json`.

## Flow

```
Claude Code PermissionRequest
  → HTTP POST (application/json) to Worker
  → DeviceAgent.onRequest dispatches by path
  → handlePermissionHook parses payload, derives summary
  → broadcast {"type":"app_event","name":"permission","data":{tool, summary}}
    to both "device" and "monitor" connections
  → 204 No Content back to Claude Code
  → Device: base Outrun::Device::onMessage routes to sandbox
  → Lua app's on_event(ctx, e) fires with e.name == "permission"
```

## Server Endpoint

**Path:** `POST /agents/device-agent/m5stick-demo/hook/permission-request`

Routed by `DeviceAgent.onRequest` based on `new URL(request.url).pathname`.
The existing `POST /agents/device-agent/m5stick-demo` (text/plain → Lua app)
behaviour is unchanged.

**Request body:** the raw `PermissionRequest` hook payload as sent by Claude
Code. Fields this endpoint reads:

- `tool_name` (string) — e.g. `"Bash"`, `"Edit"`, `"Write"`.
- `tool_input` (object) — tool-specific. See summary derivation below.

All other fields (`session_id`, `cwd`, `permission_mode`,
`permission_suggestions`, etc.) are ignored for now.

**Response:** always `204 No Content` with empty body. Never an error, never a
decision. Malformed JSON, missing fields, or internal failures are logged and
still return 204 — Claude Code's hook docs say non-2xx responses are
non-blocking, but returning a clean 204 keeps Claude Code's side quiet.

## Summary Derivation

`data.summary` is a single human-readable string. The endpoint picks one
field from `tool_input` based on `tool_name`:

| Tool        | Field             |
|-------------|-------------------|
| `Bash`      | `command`         |
| `Edit`      | `file_path`       |
| `Write`     | `file_path`       |
| `Read`      | `file_path`       |
| `Glob`      | `pattern`         |
| `Grep`      | `pattern`         |
| `WebFetch`  | `url`             |
| `WebSearch` | `query`           |
| `Task`      | `description`     |
| *fallback*  | first string value in `tool_input`, or `""` |

The picked string is truncated to 180 characters. This keeps the serialised
`data` JSON under the Outrun sandbox's 256-byte per-event buffer
(`OutrunDevice.cpp` — `char dataJson[256]`).

## Broadcast Shape

```json
{
  "type": "app_event",
  "name": "permission",
  "data": {
    "tool": "Bash",
    "summary": "rm -rf node_modules"
  }
}
```

`data` must be a flat object (no nesting, no arrays) — the sandbox's event
JSON parser drops non-flat fields.

Sent to every WebSocket on the DO tagged `"device"` and every one tagged
`"monitor"`, matching the existing pattern in `onRequest` for Lua app
broadcasts. The web UI can later render an activity feed from monitor
frames.

## Device Side

**Unchanged.** The base `Outrun::Device::onMessage` in
`outrun/src/OutrunDevice.cpp:115-139` already handles
`type: "app_event"` — it reads `name` and `data`, then calls
`_sandbox.sendAppEvent(name, dataJson)`. Whatever Lua app is currently
loaded receives the event through `on_event(ctx, e)`:

```lua
function on_event(ctx, e)
  if e.name == "permission" then
    -- e.data.tool     string, e.g. "Bash"
    -- e.data.summary  string, truncated to <= 180 chars
  end
end
```

Designing the on-device UX (a Lua app that renders the permission prompt) is
**out of scope for this spec** — that lands in a follow-up.

## README Addition

Add a new section to `examples/m5stick-claude-code/README.md`:

````markdown
## Claude Code permission notifier (optional)

Get a nudge on your M5Stick every time Claude Code asks for permission to run
a tool. Add this to your project's `.claude/settings.json` (or
`~/.claude/settings.json` for every session):

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

Claude Code POSTs the permission payload to the Worker, which broadcasts a
`permission` app event to the connected device. The hook always returns
`204 No Content`, so Claude Code still prompts you in the terminal — you
approve or deny there as normal.

The currently-loaded Lua app receives the event via `on_event`:

```lua
function on_event(ctx, e)
  if e.name == "permission" then
    -- e.data.tool     string, e.g. "Bash"
    -- e.data.summary  string, e.g. "rm -rf node_modules"
  end
end
```

### Test it without Claude Code

With the device connected, send a synthetic payload:

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"tool_name":"Bash","tool_input":{"command":"rm -rf node_modules"}}' \
  https://m5stick-demo.genmon.workers.dev/agents/device-agent/m5stick-demo/hook/permission-request
```

The device's currently-loaded Lua app should see an event with
`name == "permission"` and `data == { tool = "Bash", summary = "rm -rf node_modules" }`.
````

## Testing

**Unit** — exercise `handlePermissionHook` directly:

- Valid Bash payload → broadcast has `tool: "Bash"`, `summary: <command>`.
- Valid Edit payload → `tool: "Edit"`, `summary: <file_path>`.
- Unknown tool → `summary` falls back to first string field or `""`.
- Oversized command (>180 chars) → `summary` is truncated to 180.
- Malformed JSON body → returns 204, no broadcast, logs a warning.
- No `tool_name` → returns 204, no broadcast, logs a warning.

Use whatever framework the worker project already has; add `vitest` if there
isn't one.

**Manual** — the curl invocation above, with the device connected and a Lua
app running that logs `on_event` payloads.

## Non-Goals

- On-device UX for displaying the permission. Later.
- Filtering by tool name, cwd, or session. All `PermissionRequest` events
  fire the hook.
- Authentication on the hook endpoint. Same public Worker URL; worst case
  someone can trigger a broadcast to the one DO.
- Supporting other hook event types (`PreToolUse`, `Notification`, etc.).
- Adapting the hook for multiple devices / device IDs.

## Open Questions

None for this slice. The on-device UX decision is deferred to a follow-up
spec.
