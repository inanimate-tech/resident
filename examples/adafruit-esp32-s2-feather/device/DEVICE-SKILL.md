# Adafruit ESP32-S2 TFT Feather

The Adafruit ESP32-S2 TFT Feather is a single-core ESP32-S2 board with a 1.14"
240×135 ST7789 TFT, an onboard NeoPixel, and an LC709203F LiPo fuel gauge.
Apps drive the screen as their primary output, can light the NeoPixel, and
read battery state.

## Hardware

**TFT screen:** ST7789, 135×240 pixels in **portrait** orientation
(135 wide × 240 tall), USB-C at the bottom. Coordinates 0-based, origin
top-left. Colours 0–255 per channel. Double-buffered: draw to an
off-screen canvas, then `screen.flip()` to push the frame.

**NeoPixel:** A single addressable RGB LED on the front of the board.

**Battery monitor:** LC709203F fuel gauge over I2C at address `0x0B`. Only
responds when a LiPo is connected to the JST connector (the chip is powered
by VBAT, so without a battery it's invisible).

## Lua Modules

### screen.*
**Hardware:** ST7789 TFT, 135×240 portrait, double-buffered.

```lua
-- Clear the off-screen canvas
screen.clear()                                   -- clear to black
screen.clear(255, 0, 0)                          -- clear to red

-- Draw text (defaults: size 2, white)
screen.text(10, 10, "HELLO")                     -- size 2, white
screen.text(10, 10, "HELLO", 3)                  -- size 3, white
screen.text(10, 10, "HELLO", 2, 255, 0, 0)       -- size 2, red

-- Shapes
screen.fill_rect(x, y, w, h, r, g, b)            -- filled rectangle
screen.rect(x, y, w, h, r, g, b)                 -- 1px outline rectangle
screen.line(x0, y0, x1, y1, r, g, b)             -- 1px line
screen.triangle(x0, y0, x1, y1, x2, y2, r, g, b) -- 1px outline triangle
screen.fill_triangle(x0, y0, x1, y1, x2, y2, r, g, b)
screen.pixel(x, y, r, g, b)                      -- single pixel

-- Push the canvas to the display (call once per frame)
screen.flip()

-- Backlight PWM (0-255). 0 = off, 255 = full bright.
screen.set_brightness(128)

-- Query dimensions
local w = screen.width()                         -- 135
local h = screen.height()                        -- 240
```

**MUST:** call `screen.flip()` after every draw sequence — nothing is
visible until you flip. On app reset the canvas is cleared and pushed.

### led.*
**Hardware:** Onboard NeoPixel (single pixel).

```lua
led.set(255, 0, 0)        -- set colour (r, g, b each 0-255)
led.set_brightness(64)    -- global brightness 0-255 applied on next set()
led.off()                  -- turn the pixel off
```

The pixel is bright — clamp brightness with `led.set_brightness(20)` or
similar in `init()` for indoor use.

### battery.*
**Hardware:** LC709203F fuel gauge (only responds when a LiPo is plugged in).

```lua
local v = battery.voltage()    -- cell voltage in V (0.0 if no battery)
local p = battery.percent()    -- state of charge 0-100 (0.0 if no battery)
local connected = battery.present()  -- true if a battery is plugged in
```

The percentage takes ~30 seconds to converge after boot — the first few
readings can be misleading.

## Examples

**Hello world:**

```lua
function init(ctx)
  screen.clear()
  screen.text(10, 50, "HELLO WORLD", 2, 0, 255, 0)
  screen.text(10, 80, "from Resident", 2)
  screen.flip()
end
```

**Bouncing ball:**

```lua
local x, y = 67, 120
local dx, dy = 1.5, 2
local r = 8

function on_tick(ctx, dt_ms)
  x = x + dx
  y = y + dy
  if x <= r or x >= 135 - r then dx = -dx end
  if y <= r or y >= 240 - r then dy = -dy end

  screen.clear()
  screen.fill_rect(math.floor(x - r), math.floor(y - r), r * 2, r * 2, 0, 255, 0)
  screen.flip()
end
```

**Battery readout:**

```lua
function on_tick(ctx, dt_ms)
  screen.clear()
  screen.text(5, 10, "Battery", 2, 0, 255, 255)
  if battery.present() then
    screen.text(5, 40, string.format("%.2f V", battery.voltage()), 2)
    screen.text(5, 70, string.format("%.0f %%", battery.percent()), 2)
  else
    screen.text(5, 40, "Not plugged in", 1, 255, 255, 0)
  end
  screen.flip()
end
```

**Pulse the NeoPixel:**

```lua
function on_tick(ctx, dt_ms)
  local phase = (ctx.time_ms % 2000) / 2000
  local v = math.floor(255 * (0.5 + 0.5 * math.sin(phase * 2 * math.pi)))
  led.set(0, v, 0)
end
```

## Constraints

- Screen: 135×240 portrait, 0-based coords, 0–255 colour channels.
- Single NeoPixel — `led.set(r, g, b)` takes one RGB triple.
- Battery: needs a LiPo on the JST connector or `battery.*` returns zeros.
- No buttons. No IMU. No buzzer. No audio.

## Practical Tips

- Always call `screen.flip()` after a draw sequence — double-buffered.
- Default text size is 2; use size 1 for dense info, size 3 for headings.
- The NeoPixel is bright — clamp brightness with `led.set_brightness(20)`.
- Use `battery.present()` before drawing battery readings to avoid showing
  meaningless zeros when there's no LiPo connected.

## Validation stubs

```lua
screen = setmetatable({
  width  = function() return 135 end,
  height = function() return 240 end,
}, { __index = function() return function() end end })

battery = setmetatable({
  voltage = function() return 3.95 end,
  percent = function() return 78.0 end,
  present = function() return true end,
}, { __index = function() return function() end end })

led = setmetatable({}, { __index = function() return function() end end })
```

## App mode / Shader mode

In app mode this device uses the normal app lifecycle (`init`, `on_tick`,
`on_event`) against the `screen`, `led`, and `battery` modules described
above.

Shader mode is not available on this device — only app mode is supported.
