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
