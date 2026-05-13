local particles = {}
local N = 140

local LEAF = {
  {255, 120,  30},
  {220, 180,  40},
  {180,  60,  30},
  {200, 100,  20},
  {140,  90,  30},
  {255,  80,  40},
  {120,  40,  20},
}

-- Wind state lerps toward a target that gets re-chosen at random
-- intervals (0.4–3.5 s). Range is biased rightward but allows full
-- reversal — Lutosław-style controlled aleatorism.
local wind_x = 1.0
local wind_y = 0.0
local wind_x_target = 1.0
local wind_y_target = 0.0
local next_change = 0

local function newWindTarget(t)
  wind_x_target = -1.2 + math.random() * 2.8
  wind_y_target = -0.6 + math.random() * 1.2
  next_change = t + 0.4 + math.random() * 3.1
end

function init(ctx)
  led.set_brightness(80)
  for i = 1, N do
    local c = LEAF[(i % #LEAF) + 1]
    particles[i] = {
      x = math.random() * 135,
      y = math.random() * 240,
      vx = 20 + math.random() * 55,
      vy_base = math.random() * 5,
      phase = math.random() * 6.283,
      twirl = 4 + math.random() * 16,
      resp = 0.7 + math.random() * 0.6,   -- per-particle wind responsiveness
      r = c[1], g = c[2], b = c[3],
    }
  end
  newWindTarget(0)
end

function on_tick(ctx, dt_ms)
  local dt = dt_ms / 1000
  local t = ctx.time_ms / 1000

  if t > next_change then newWindTarget(t) end
  local lerp = 1 - math.exp(-1.4 * dt)
  wind_x = wind_x + (wind_x_target - wind_x) * lerp
  wind_y = wind_y + (wind_y_target - wind_y) * lerp

  -- Composite gust: three incommensurable frequencies, never repeats
  local gust = 0.5
    + 0.3 * math.sin(t * 0.7)
    + 0.2 * math.sin(t * 1.9 + 1.3)
    + 0.15 * math.sin(t * 3.1 + 2.7)
  if gust < 0.15 then gust = 0.15 end

  screen.clear()

  for i = 1, N do
    local p = particles[i]
    local fx = wind_x * p.vx * gust * p.resp
    local fy = wind_y * p.vx * gust * 0.5 * p.resp
              + p.vy_base
              + p.twirl * math.sin(t * 1.5 + p.phase)
    p.x = p.x + fx * dt
    p.y = p.y + fy * dt

    if p.x > 137 then p.x = -2; p.y = math.random() * 240 end
    if p.x < -2  then p.x = 137; p.y = math.random() * 240 end
    if p.y > 242 then p.y = -2 end
    if p.y < -2  then p.y = 242 end

    screen.fill_rect(math.floor(p.x), math.floor(p.y), 4, 2, p.r, p.g, p.b)
  end

  -- NeoPixel encodes wind state: red when blowing right, blue when reversed.
  local v = 0.4 + 0.5 * gust
  local r = math.floor((127 + 100 * wind_x) * v)
  local b = math.floor((127 - 100 * wind_x) * v)
  if r < 0 then r = 0 elseif r > 255 then r = 255 end
  if b < 0 then b = 0 elseif b > 255 then b = 255 end
  led.set(r, math.floor(40 * v), b)

  screen.flip()
end
