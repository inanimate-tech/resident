-- Pure black-and-white plasma with Lutosław wind chaos. Same flow as
-- embers.lua but in 9 grayscale stops, from pure black to pure white.

local PALETTE = {
  {  0,   0,   0},
  { 32,  32,  32},
  { 64,  64,  64},
  { 96,  96,  96},
  {128, 128, 128},
  {160, 160, 160},
  {192, 192, 192},
  {224, 224, 224},
  {255, 255, 255},
}

local sin = math.sin
local floor = math.floor
local exp = math.exp
local PSIZE = 12
local STEPS = #PALETTE - 1

local phase_x = 0
local phase_y = 0
local phase_t = 0

local wind_x = 1.0
local wind_y = 0.0
local wind_x_target = 1.0
local wind_y_target = 0.0
local next_change = 0

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

  -- NeoPixel: monochrome white that brightens with gust.
  local lv = floor(50 + 200 * gust)
  if lv > 255 then lv = 255 end
  led.set(lv, lv, lv)

  screen.flip()
end
