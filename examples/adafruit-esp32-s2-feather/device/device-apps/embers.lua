-- Autumn-palette plasma with Lutosław wind chaos. The pattern flows in
-- the direction the wind is blowing — when wind reverses, the plasma
-- reverses with it. Gust modulates global speed.

-- Spring cherry-blossom palette. Deep azure at the low end (not near-
-- black — distinctly blue), through royal blue / sky / lavender,
-- peaking at vivid sakura pink with high punch.
local PALETTE = {
  { 25,  55, 120},
  { 45,  90, 165},
  { 85, 140, 195},
  {135, 185, 215},
  {180, 215, 235},
  {220, 210, 235},
  {250, 200, 225},
  {255, 175, 225},
  {255, 140, 230},
}

local sin = math.sin
local floor = math.floor
local exp = math.exp
local PSIZE = 12
local STEPS = #PALETTE - 1

-- Three phase accumulators. phase_x advances with wind_x (reversible),
-- phase_y with wind_y, phase_t with raw time. All scaled by gust.
local phase_x = 0
local phase_y = 0
local phase_t = 0

local wind_x = 1.0
local wind_y = 0.0
local wind_x_target = 1.0
local wind_y_target = 0.0
local next_change = 0

-- Reusable caches — avoid per-frame allocation.
local col_cache = {}
local row_cache = {}

local function newWindTarget(t)
  wind_x_target = -1.0 + math.random() * 2.5
  wind_y_target = -0.8 + math.random() * 1.6
  next_change = t + 0.4 + math.random() * 3.0
end

function init(ctx)
  led.set_brightness(80)
  newWindTarget(0)
end

function on_tick(ctx, dt_ms)
  local t = ctx.time_ms / 1000
  local dt = dt_ms / 1000

  if t > next_change then newWindTarget(t) end
  local lerp = 1 - exp(-1.4 * dt)
  wind_x = wind_x + (wind_x_target - wind_x) * lerp
  wind_y = wind_y + (wind_y_target - wind_y) * lerp

  local gust = 0.6
    + 0.4  * sin(t * 0.7)
    + 0.2  * sin(t * 1.9 + 1.3)
    + 0.15 * sin(t * 3.1 + 2.7)
  if gust < 0.2 then gust = 0.2 end

  phase_x = phase_x + dt * gust * wind_x * 4
  phase_y = phase_y + dt * gust * wind_y * 4
  phase_t = phase_t + dt * gust * 2

  -- Hoist per-column and per-row sines out of the inner loop.
  for x = 0, 132, PSIZE do col_cache[x] = sin(x * 0.05 + phase_x) end
  for y = 0, 228, PSIZE do row_cache[y] = sin(y * 0.04 + phase_y) end

  local fr = screen.fill_rect

  for y = 0, 228, PSIZE do
    local ry = row_cache[y]
    for x = 0, 132, PSIZE do
      local v = col_cache[x] + ry
              + sin((x + y) * 0.03 - phase_t)
              + sin((x - y) * 0.025 + phase_t * 0.7)
      local norm = (v + 4) / 8
      if norm < 0 then norm = 0 elseif norm > 1 then norm = 1 end
      local idx = floor(norm * STEPS) + 1
      local c = PALETTE[idx]
      fr(x, y, PSIZE, PSIZE, c[1], c[2], c[3])
    end
  end

  -- NeoPixel: dim pink at low gust, vivid pink at high gust. Stays in
  -- the cherry-blossom palette rather than encoding direction.
  local lv = floor(50 + 200 * gust)
  if lv > 255 then lv = 255 end
  led.set(lv, floor(lv * 0.55), floor(lv * 0.90))

  screen.flip()
end
