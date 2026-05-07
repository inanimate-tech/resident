# M5StickC Plus2

The M5StickC Plus2 is a wrist-mountable ESP32 device with a 135×240 colour TFT,
a 6-axis IMU, two physical buttons, and a piezo buzzer. Apps drive the screen
as their primary output and respond to button presses + motion.

## Hardware

**TFT screen:** ST7789V2, 135×240 pixels, landscape orientation (240 wide,
135 tall). Coordinates 0-based, origin top-left. Colours 0–255 per channel.
Double-buffered: draw to an off-screen sprite, then `screen.flip()` to push
the frame.

**IMU:** 6-axis (MPU6886). Body frame: X = long axis (USB toward −X), Y =
short axis, Z = screen normal (positive points out of the screen toward the
viewer). At rest face-up, `accel()` returns approximately `(0, 0, +1)` in g.
Gyro is in degrees/second.

**Buzzer:** Piezo. Frequency range 100–8000 Hz, duration 10–5000 ms.

**Buttons:** Two physical buttons, indexed 0 and 1. Surface as `button` events
with an `index` field.

## Lua Modules

### screen.*
**Hardware:** ST7789V2 TFT, 240×135 landscape, double-buffered.

```lua
-- Clear the off-screen sprite
screen.clear()                                   -- clear to black
screen.clear(255, 0, 0)                          -- clear to red

-- Draw text (defaults: size 2, white)
screen.text(10, 10, "HELLO")                     -- size 2, white
screen.text(10, 10, "HELLO", 3)                  -- size 3, white
screen.text(10, 10, "HELLO", 2, 255, 0, 0)       -- size 2, red

-- Draw a QR code (auto-picks QR v3..v10, ECC low). Caller is responsible
-- for a light background behind the QR for scannability.
screen.qr(62, 10, "https://example.com")          -- scale 4, black
screen.qr(62, 10, "https://example.com", 3)       -- scale 3, black
screen.qr(62, 10, "https://example.com", 4, 0, 0, 128)  -- navy

-- Shapes
screen.fill_rect(x, y, w, h, r, g, b)            -- filled rectangle
screen.rect(x, y, w, h, r, g, b)                 -- 1px outline rectangle
screen.line(x0, y0, x1, y1, r, g, b)             -- 1px line
screen.triangle(x0, y0, x1, y1, x2, y2, r, g, b) -- 1px outline triangle
screen.fill_triangle(x0, y0, x1, y1, x2, y2, r, g, b)
screen.pixel(x, y, r, g, b)                      -- single pixel

-- Push the sprite to the display (call once per frame)
screen.flip()

-- Settings
screen.set_brightness(128)                       -- 0–255

-- Query dimensions
local w = screen.width()                         -- 240
local h = screen.height()                        -- 135
```

**MUST:** call `screen.flip()` after every draw sequence — nothing is
visible until you flip. On app reset the screen is cleared to black.

### imu.*
**Hardware:** MPU6886 6-axis.

```lua
local ax, ay, az = imu.accel()  -- g-force, body frame
local gx, gy, gz = imu.gyro()   -- degrees/second, body frame
local t          = imu.temp()   -- stub, returns 0
```

Gyro measures rate, so readings spike during motion and return to ~0 at
rest. For absolute orientation use `accel()` (tilt) — yaw around +Z is not
observable without a magnetometer.

### buzzer.*
**Hardware:** Piezo.

```lua
buzzer.beep(440, 200)   -- frequency Hz, duration ms
buzzer.tone(1000)       -- start continuous tone
buzzer.stop()           -- stop tone
```

Frequency 100–8000 Hz; duration 10–5000 ms. Tone is stopped on app reset.

### button.*
**Hardware:** 2 physical buttons, indices 0 and 1.

```lua
local count = button.press_count()  -- total presses since boot
```

Best practice: handle button presses in `on_event(ctx, e)` rather than
polling. The event has a `name == "button"` and an `index` field (0 or 1).

## Examples

**Hello world:**

```lua
function init(ctx)
  screen.clear()
  screen.text(10, 10, "HELLO WORLD")
  screen.flip()
end
```

**Bouncing ball:**

```lua
local x, y = 120, 67
local dx, dy = 2, 1.5
local r = 8

function on_tick(ctx, dt_ms)
  x = x + dx
  y = y + dy
  if x <= r or x >= 240 - r then dx = -dx end
  if y <= r or y >= 135 - r then dy = -dy end

  screen.clear()
  screen.fill_rect(math.floor(x - r), math.floor(y - r), r * 2, r * 2, 0, 255, 0)
  screen.flip()
end
```

**Spirit level:**

```lua
function on_tick(ctx, dt_ms)
  local ax, ay, az = imu.accel()
  screen.clear()

  local cx = math.floor(120 + ax * 100)
  local cy = math.floor(67 + ay * 100)
  cx = math.max(5, math.min(235, cx))
  cy = math.max(5, math.min(130, cy))

  screen.fill_rect(118, 0, 4, 135, 50, 50, 50)
  screen.fill_rect(0, 65, 240, 4, 50, 50, 50)
  screen.fill_rect(cx - 5, cy - 5, 10, 10, 0, 255, 0)
  screen.flip()
end
```

**Two-button counter with beep:**

```lua
local count = 0

function init(ctx)
  draw()
end

function draw()
  screen.clear()
  screen.text(10, 10, "Count: " .. count)
  screen.text(10, 50, "Btn0: +1")
  screen.text(10, 70, "Btn1: reset")
  screen.flip()
end

function on_event(ctx, e)
  if e.name == "button" then
    if e.index == 0 then
      count = count + 1
      buzzer.beep(440, 50)
    else
      count = 0
      buzzer.beep(880, 50)
    end
    draw()
  end
end
```

**Shake detection:**

```lua
local shake_threshold = 2.0

function on_tick(ctx, dt_ms)
  local ax, ay, az = imu.accel()
  local mag = math.sqrt(ax*ax + ay*ay + az*az)
  if mag > shake_threshold then
    buzzer.beep(800, 100)
    screen.clear(255, 0, 0)
  else
    screen.clear(0, 50, 0)
  end
  screen.flip()
end
```

## Constraints

- Screen: 240×135 landscape, 0-based coords, 0–255 colour channels.
- IMU: g-force for accel, deg/sec for gyro. `temp()` stubbed.
- Buzzer: 100–8000 Hz, 10–5000 ms.
- Two buttons only — indices 0 and 1.

## Practical Tips

- The screen is the primary output surface. Don't rely on the LED.
- Display orientation is landscape (240 wide × 135 tall). Authors who
  forget this draw vertical apps that look wrong.
- Always call `screen.flip()` after a draw sequence — double-buffered.
- Use `e.index` (0 or 1) to distinguish buttons in `on_event`.
- Precompute lookup tables in `init()` to avoid math in hot paths.
- For shake detection, threshold against magnitude, not individual axes.

## Validation stubs

Optional Lua block consumed by `validate-app` to provide concrete return
values for getter-style functions. Without these, getter calls return
`nil` and apps that do arithmetic on the result crash inside the
validator.

```lua
screen = setmetatable({
  width  = function() return 240 end,
  height = function() return 135 end,
}, { __index = function() return function() end end })

imu = setmetatable({
  accel = function() return 0, 0, 1 end,  -- face-up resting orientation
  gyro  = function() return 0, 0, 0 end,
  temp  = function() return 0 end,
}, { __index = function() return function() end end })

button = setmetatable({
  press_count = function() return 0 end,
}, { __index = function() return function() end end })

buzzer = setmetatable({}, { __index = function() return function() end end })
```
