# <Device name>

<one-paragraph overview: what the device is, what kind of apps suit it>

## Hardware

<physical description of each piece of hardware: screens, sensors, audio,
indicators, buttons. Include coordinate frames and units the Lua side
will see (g-force, Hz, ms, etc.).>

## Lua Modules

### <module-name>.*
**Hardware:** <one-line>

```lua
-- Function-by-function reference: signatures, defaults, ranges.
```

(Repeat per driver module the firmware registers.)

## Examples

<3–6 short, working Lua apps that exercise the device modules. Tight.>

## Constraints

<screen dimensions, frequency/duration ranges, memory limits — anything
that shapes generated code.>

## Practical Tips

<device-specific idioms (shake detection, menu nav, etc.). Optional.>

## Validation stubs

Optional. Consumed by `validate-app` to provide concrete return values
for getter-style functions (e.g. `screen.width()`, `imu.accel()`).
Without these, getters return `nil` and apps that do arithmetic on the
result crash inside the validator.

```lua
<module> = setmetatable({
  width  = function() return <number> end,
  height = function() return <number> end,
}, { __index = function() return function() end end })

-- Repeat for any module whose getters return numbers, strings, or
-- multiple values that apps actually consume.
```

## App mode / Shader mode

In app mode this device uses the normal app lifecycle (`init`,
`on_tick`, `on_event`). In shader mode, a single expression is
evaluated <how often / on what trigger — e.g. "once per pixel per
frame", "once per tick"> and the result is used to control <what the
return value drives on the device — e.g. "the colour of each pixel",
"the angle of the servo">.

Additional variables are available on each evaluation in shader mode
only (on top of the sandbox-generic `ctx.time_ms`, `ctx.trigger_count`,
the time fields, and the shader-compatible globals `rgb` / `fract` /
`beat` / `noise2d` / math globals):

| Variable | Type | Description |
|----------|------|-------------|
| `<name>` | `<type>` | <what it represents, range, units> |

If this device does not support shader mode, replace the body of this
section with a single sentence: "Shader mode is not available on this
device — only app mode is supported." Omit the table. Do not describe
how the firmware could be extended to add shader mode — that is not
useful to an app author and does not belong in this document.
