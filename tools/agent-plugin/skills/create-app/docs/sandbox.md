<!-- SOURCE: prompts/sandbox.md — canonical copy lives there, versioned
     with the code it documents. Edit THAT file and re-copy here. -->

# Resident sandbox — Lua surface

You are writing a Lua app for a small connected device. This sheet is the
universal surface every Resident device exposes. Device-specific modules
(screen, sensors, buttons, outputs) are documented in the sheets appended
after this one.

## Lifecycle

Define at least one of these globals (the upload is rejected otherwise):

```lua
function init(ctx)              -- once, after load
end

function on_tick(ctx, dt_ms)    -- every 100 ms; dt_ms = real elapsed ms
end

function on_event(ctx, e)       -- once per queued event
end
```

The app keeps running across disconnects. Errors in a callback are contained:
that dispatch dies (and is reported), the app survives.

## ctx table

Identical in every callback:

| Field | Type | Meaning |
|-------|------|---------|
| `time_ms` | integer | ms since this app loaded — the app's clock |
| `trigger_count` | integer | count of `button` driver events since boot |
| `generation_id` | string or nil | the server's id for this program version |
| `utc_h`, `utc_m` | integer | UTC wall clock |
| `localtime_h`, `localtime_m` | integer | local wall clock (equals UTC until a timezone is set) |

Use `ctx.time_ms` for animation. Use `ctx.localtime_h/m` for time-of-day
behavior.

## Events in (`on_event`)

Every event has `e.name`, `e.from` (empty for hardware), `e.ts_ms`,
`e.channel` (`"driver"` for hardware, `"app"`/`"runtime"` for the wire) —
and its payload in `e.data`, ONE shape for every event: strings, numbers,
booleans, tables nested to depth 3.

```lua
function on_event(ctx, e)
  if e.name == "button" then
    log.info("button " .. tostring(e.data.index))
  elseif e.name == "note" then
    log.info("text: " .. tostring(e.data.text))
  end
end
```

Events queue in an 8-slot ring; a burst beyond that silently drops the
oldest. An incoming `data` payload over 1024 serialized bytes drops the whole
event (never truncated).

## Events out (`events` module)

```lua
events.send("report", { level = 3, note = "ok" })   -- -> boolean
```

- Publishes to the server on the app channel. Returns `false` when
  rate-limited, oversize, or offline — nothing is sent then.
- Rate limit: 5 events/s sustained, burst of 10.
- `data` values: strings, numbers, booleans, tables to depth 3. Serialized
  size cap 1024 bytes; oversize is dropped, never truncated.
- Absent on some surfaces (feature-detect: `type(events) == "table"`); calls
  are then meaningless — guard or skip.

## Persistent state (`store` module)

An app-scoped KV slot of SCALARS that survives app reloads and reboots
(cleared automatically when a different app is installed):

```lua
local n = store.get("count") or 0    -- string | number | boolean | nil
store.set("count", n + 1)            -- -> boolean; nil value deletes
store.keys()                          -- array of key strings
store.clear()
```

- `store.set` returns `false` (and changes nothing) for non-scalar values or
  when the total slot budget — 2048 serialized bytes — would be exceeded.
- Writes persist after ~2 s of quiet (at latest every 30 s), and immediately
  on app unload. Don't write per-tick values you don't need back.
- Absent on some surfaces (feature-detect: `type(store) == "table"`).

## log module

```lua
log.info("hello")  log.warn("careful")  log.error("broke")
```

`log.error` also reports upstream as telemetry.

## time module

NTP wall clock. UTC unless the device has a timezone.

`time.is_valid()` · `time.has_timezone()` · `time.hour()` · `time.minute()`
· `time.second()` · `time.day_id()` (days since boot — a daily cache key).

## Always-global functions

`rgb(r,g,b)` (normalized floats → packed color, negative-int sentinel) ·
`fract(x)` · `beat(bpm, t)` · `noise2d(x, y)` (-1..1) — plus bare math:
`floor ceil abs sin cos tan sqrt min max fmod`.

## Limits

| Limit | Value |
|-------|-------|
| tick rate | 10/s (100 ms) |
| event ring | 8 slots, oldest dropped |
| event data (both directions) | 1024 bytes serialized, drop not truncate |
| events.send rate | 5/s sustained, burst 10 |
| store budget | 2048 bytes total, rejected whole |
| repeated on_tick errors | 3, then one per 5 s |

Keep apps short. A tight app survives device memory limits; a sprawling one
may not load at all.
