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

Use `ctx.time_ms` for animation. For the date and time of day, use the
`datetime` module (`datetime.now()`).

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

## datetime module

Python's `datetime`, with whole seconds. If you know Python's `datetime`, you
know this; `datetime(...)` and `datetime.datetime(...)` both construct.

```lua
if not datetime.synced() then return end      -- until the network sets the clock, now() is 1970
local now = datetime.now()                    -- local time; UTC until a zone is set
log.info(now:strftime("%a %H:%M"))            -- "Mon 13:05"
local evening = now.hour >= 18
local weekend = now:weekday() >= 5            -- Monday = 0
local left = (datetime.date(2026, 12, 25) - datetime.today()).days
local tomorrow = datetime.today() + datetime.timedelta{ days = 1 }
local alarm = datetime(2026, 12, 25, 7, 30)   -- local
if now >= alarm then log.info("ring") end
```

- `datetime.now([tz])` → datetime; `datetime.today()` → a **date** (local);
  `datetime.fromtimestamp(secs[, tz])`; `datetime.synced()` → boolean.
- `datetime(y, mo, d[, h, mi, s[, tz]])`, `datetime.date(y, mo, d)`,
  `datetime.timedelta{ weeks, days, hours, minutes, seconds }` (a bare number
  is days).
- Two zones: local (the device's, DST applied) and `datetime.UTC`. A `tz`
  argument is `datetime.UTC`, or nil for local.
- datetime: fields `year month day hour minute second tzinfo`; `weekday()`
  (Monday = 0), `isoweekday()` (Monday = 1), `date()`, `timestamp()`,
  `strftime(fmt)`, `isoformat()`, `replace{ hour = 0, ... }`,
  `astimezone([tz])`, `tzname()` (`"BST"`), `utcoffset()` (a timedelta).
- date: fields `year month day`; `weekday()`, `isoweekday()`, `toordinal()`,
  `strftime(fmt)`, `isoformat()`, `replace{ ... }`.
- timedelta: fields `days`, `seconds` (0..86399; the sign is on `days`);
  `total_seconds()`.
- `+` `-` as in Python (datetime − datetime and date − date give a
  timedelta), `timedelta * integer`, `==` `<` `<=`. Within one zone,
  arithmetic is on the wall clock: noon plus a day is noon, across DST. A date
  never equals a datetime, and ordering one against the other raises.
- `tostring(x)` is Python's `str()`: `2026-10-05 13:05:09`, `2026-10-05`,
  `1 day, 2:00:00`.
- `strftime`: `%a %A %b %B %c %d %e %H %I %j %m %M %p %S %U %w %W %x %X %y %Y
  %Z %z %%` (C locale, English names), max 255 bytes.
- Treat values as immutable: make new ones with `replace{}` and arithmetic.
- No microseconds. Years 1..9999; what needs epoch seconds (`now`,
  `timestamp()`, a local `utcoffset()`, mixing zones) stops at 2038-01-19.
- `datetime.today():toordinal()` is a good once-a-day key.

## time module

Elapsed time: a wrapping millisecond counter, read through `ticks_diff`,
which is right across the wrap.

```lua
local t0 = time.ticks_ms()
-- later:
local ms = time.ticks_diff(time.ticks_ms(), t0)   -- a - b in ms
```

Use ticks for timeouts and durations: they do not jump when the clock is set,
and `ctx.time_ms`, a plain count since load, goes wrong after ~24.8 days.

`time.time`, `localtime`, `gmtime`, `mktime`, `strftime` and `synced` are
deprecated — they still work, and warn once per app load. Use
`datetime.now()`, `dt:timestamp()`, `datetime.fromtimestamp(secs)`,
`datetime(...)`, `dt:strftime(fmt)` and `datetime.synced()`.

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

- `screens.list()` → each `{ name, w, h, shape, depth, scheme, dpi?, group? }`;
  `shape` is `"rect"` or `"round"`, `depth` 16 (colour) or 1 (one-bit
  glass). A board with no screen lists nothing. These are the names
  `lvgl.bind(name)` takes.
- `scheme` is `"dark"` (light marks on a dark ground: glass that emits
  light — TFT, LED, VFD — where blank is unlit) or `"light"` (dark marks on a
  light ground: e-paper, a reflective STN — where blank is the paper). Pick
  colours for it: a dark ground on a light screen is ink everywhere.
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
