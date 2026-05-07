# Resident Sandbox — Lua API (for app authors)

This is the surface every Resident-based device exposes to Lua apps. For
device-specific modules (screen, imu, buzzer, etc.), see the project's
DEVICE-SKILL.md.

## App lifecycle

An app must define at least one of these globals. If none are defined,
the device rejects the upload.

```lua
function init(ctx)
  -- called once after compilation; one-time setup
end

function on_tick(ctx, dt_ms)
  -- called at 10 FPS; dt_ms is elapsed ms since the last tick
end

function on_event(ctx, event)
  -- called for each queued event (driver events and app_events)
end
```

## ctx table

Every callback receives a `ctx` table with these fields:

| Field | Type | Description |
|-------|------|-------------|
| `time_ms` | integer | Milliseconds since the current app was loaded |
| `trigger_count` | integer | Number of `"button"` driver events since the last app load |
| `utc_h` | integer | Current UTC hour (0–23) |
| `utc_m` | integer | Current UTC minute (0–59) |
| `localtime_h` | integer | Local hour — equals `utc_h` unless a timezone has been set |
| `localtime_m` | integer | Local minute — equals `utc_m` unless a timezone has been set |
| `day_id` | integer | Days since boot |

Use `ctx.time_ms` for animations (counts from app load, no reset).
Use `ctx.trigger_count` for mode switching.
Use `ctx.localtime_h/m` for time-of-day effects.

## event table

`on_event(ctx, event)` receives:

| Field | Type | Description |
|-------|------|-------------|
| `event.name` | string | Event name (e.g. `"button"`, custom names from app_events) |
| `event.from` | string | Source identifier — empty for driver events |
| `event.ts_ms` | integer | Timestamp in ms when the event was queued |
| *(driver fields)* | any | Driver events flatten extra fields directly (e.g. `event.index`) |
| `event.data` | table | App events: parsed JSON `data` object as a Lua table |

```lua
function on_event(ctx, event)
  if event.name == "button" then
    -- driver event: e.g. event.index, event.state
    log.info("button " .. tostring(event.index))
  elseif event.name == "color_change" then
    -- app event: nested data
    log.info("hue: " .. tostring(event.data.hue))
  end
end
```

## log module

```lua
log.info("hello")
log.warn("careful")
log.error("something broke")  -- also emits a log_error telemetry event
```

## time module

NTP-backed wall clock; UTC unless the device has a timezone configured.

| Function | Returns |
|----------|---------|
| `time.is_valid()` | boolean — true once NTP has synced |
| `time.has_timezone()` | boolean — true if a timezone is set |
| `time.hour()` | integer 0–23 |
| `time.minute()` | integer 0–59 |
| `time.second()` | integer 0–59 |
| `time.day_id()` | integer — days since boot, useful as a daily-state cache key |

## kv module

Persistent string key/value store, ~4 KB total budget.

```lua
local v = kv.get("count")    -- returns string or nil
kv.set("count", "42")        -- returns true on success
```

Apps that don't need persistence can ignore `kv` — but if they call it,
the calls are safe even when the device has no storage.

## Shader-compatible globals

These functions are always in scope — designed to work both in shader
expressions and full apps.

| Function | Returns | Description |
|----------|---------|-------------|
| `rgb(r, g, b)` | integer | Pack normalized floats (0–1) into a colour (negative-int sentinel) |
| `fract(x)` | number | `x - floor(x)` |
| `beat(bpm, t)` | number | `t / (60000 / bpm)` — beat phase in beats |
| `noise2d(x, y)` | number | Deterministic 2D value noise, returns -1..+1 |

Math globals registered without the `math.` prefix:

`floor`, `ceil`, `abs`, `sin`, `cos`, `tan`, `sqrt`, `min`, `max`, `fmod`.

## App constraints

| Limit | Value |
|-------|-------|
| `on_tick` rate | 10 FPS (100 ms interval) |
| Event ring buffer | 8 events; oldest dropped when full |
| `event.data` (app_event JSON) | 256 bytes |
| Driver event field name | 32 chars |
| Runtime-error rate limit | 3 errors then cooldown of 5 s |

Apps too large for the Lua compiler crash the device. Keep apps tight —
short apps survive memory limits better than long ones.

## What's NOT in this document

This document covers the universal sandbox surface. For device-specific
modules and hardware (screen sizes, sensor units, button layouts), read
the firmware project's `DEVICE-SKILL.md`.
