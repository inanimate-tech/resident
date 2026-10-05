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

The app keeps running across disconnects — timers fire and input dispatches
offline; only network sends wait. Errors in a callback are contained: that
dispatch dies (and is reported), the app survives.

The environment is a sandbox: there is no `os`, `io`, `require`, `load`,
`dofile`, or `debug`. The pure libraries (`string`, `table`, `math`,
`coroutine`, `utf8`) are all present. Each callback runs under a wall-clock
deadline — an unbounded loop aborts that dispatch, not the device.

Numbers are 32-bit: integers wrap past ±2,147,483,647 and floats carry about
7 significant digits. Math is `math.*` only — there are no bare `floor`,
`sin`, `min` etc. globals (write `math.floor`, `math.sin`, `math.min`).

## ctx table

Identical in every callback:

| Field | Type | Meaning |
|-------|------|---------|
| `time_ms` | integer | ms since this app loaded — the app's clock |
| `generation_id` | string or nil | the server's id for this program version |

Use `ctx.time_ms` for animation. For the time of day, use the `time` module
(`time.localtime()`).

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
events.send("report", { level = 3, note = "ok" })
-- -> "sent" | "queued" | "dropped"  (sent and queued are both truthy)
events.send("must_arrive", { id = 7 }, { keep = true })
```

- Publishes to the server on the app channel. Rate-limited or offline sends
  are QUEUED and go later, in order — treat `"queued"` as success.
  `"dropped"` means it will never go (oversize payload, or the queue was
  full). `keep = true` protects a message from queue-overflow eviction.
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
store.remaining()                     -- bytes left in the budget
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

Python 3's `time`, with MicroPython's integers and ticks. If you know
Python's `time`, you know this — except there is no `time.monotonic()`,
`time.sleep()` or float seconds.

```lua
if time.synced() then                    -- false until NTP sets the clock
  local t = time.localtime()             -- local zone; UTC until one is set
  log.info(time.strftime("%a %H:%M"))    -- "Sat 15:05"
  local evening = t.tm_hour >= 18
end
local t0 = time.ticks_ms()
-- later: elapsed ms, correct across the counter's wrap
local ms = time.ticks_diff(time.ticks_ms(), t0)
```

- `time.time()` → integer seconds since 1970, UTC. Before `time.synced()`
  it counts from 1970 — check `synced()` before showing a clock.
- `time.localtime([secs])`, `time.gmtime([secs])` → struct_time table:
  `tm_year`, `tm_mon` (1..12), `tm_mday`, `tm_hour`, `tm_min`, `tm_sec`,
  `tm_wday` (0..6, **Monday = 0**), `tm_yday` (1..366), `tm_isdst` (1/0),
  `tm_zone` (`"BST"`), `tm_gmtoff` (seconds east of UTC).
- `time.mktime(t)` → integer seconds for a local struct_time
  (`tm_year`/`tm_mon`/`tm_mday` required). Out-of-range fields carry:
  `tm_mday = t.tm_mday + 1` is tomorrow.
- `time.strftime(fmt[, t])` → string; `t` defaults to `localtime()`.
  `%a %A %b %B %c %d %e %H %I %j %m %M %p %S %U %w %W %x %X %y %Y %Z %z %%`
  (C locale, English names). Max 255 bytes.
- `time.ticks_ms()` → a wrapping ms counter; only `time.ticks_diff(a, b)`
  (`a - b` in ms) of two readings means anything.
- `time.localtime().tm_yday` is a good once-a-day key.

## screens module

The board's screens: what each glass is, and its settings. Facts come from
the display drivers themselves, so they are never stale.

```lua
for _, s in ipairs(screens.list()) do
  log.info(s.name .. " " .. s.w .. "x" .. s.h .. " " .. s.shape)
end
local m = screens.get("main")         -- nil when the board has no such screen
screens.set("main", { brightness = 0.4 })
```

- `screens.list()` → each `{ name, w, h, shape, depth, dpi?, group? }`;
  `shape` is `"rect"` or `"round"`, `depth` 16 (colour) or 1 (one-bit
  glass). A board with no screen lists nothing. These are the names
  `lvgl.bind(name)` takes.
- `screens.get(name)` → that, plus the screen's current settings and status
  (`brightness`, an e-paper panel's `busy`/`pending`, …).
- `screens.set(name, { key = value, ... })` — a key the screen does not have
  raises, naming it. `brightness` and `contrast` (0..1) are the standard
  keys; a one-bit screen adds its own. Screens sharing a nonzero `group`
  share the knob (one backlight rail), so setting one sets them all.
- `screens.refresh(name)` → `true` if the screen has an "update now" (e-paper)
  and it was asked; `false` otherwise.
- Settings reset to the board's defaults whenever an app loads.

## Limits

| Limit | Value |
|-------|-------|
| tick rate | 10/s (100 ms) |
| event ring | 8 slots, oldest dropped |
| event data (both directions) | 1024 bytes serialized, drop not truncate |
| events.send rate | 5/s sustained, burst 10 |
| store budget | 2048 bytes total, rejected whole |
| work per callback | 2,000,000 Lua instructions, then the dispatch is aborted |
| repeated on_tick errors | 3, then one per 5 s |

Keep apps short. A tight app survives device memory limits; a sprawling one
may not load at all.
