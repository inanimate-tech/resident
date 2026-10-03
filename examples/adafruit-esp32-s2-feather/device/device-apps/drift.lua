-- 2D value noise in -1..1: a hashed integer lattice, bilinear between points.
-- Integer maths wraps at 32 bits on the device; the mask keeps a 64-bit Lua
-- (a desktop validator) on the same 32-bit hash.
local function hash2(x, y)
  local h = (x * 0x8da6b343 ~ y * 0xd8163841) & 0xFFFFFFFF
  h = h ~ (h >> 13)
  h = (h * 0xc2b2ae35) & 0xFFFFFFFF
  h = h ~ (h >> 16)
  return (h & 0xFFFFFF) / 0xFFFFFF
end

local function noise2d(x, y)
  local xi, yi = math.floor(x), math.floor(y)
  local xf, yf = x - xi, y - yi
  local a = hash2(xi, yi) + (hash2(xi + 1, yi) - hash2(xi, yi)) * xf
  local b = hash2(xi, yi + 1) + (hash2(xi + 1, yi + 1) - hash2(xi, yi + 1)) * xf
  return (a + (b - a) * yf) * 2 - 1
end

local W, H = 240, 135
local N = 80
local P = {}

function init(ctx)
  math.randomseed((ctx.time_ms or 0) + 11)
  for i = 1, N do
    P[i] = { math.random() * W, math.random() * H, math.random() * 1000 }
  end
end

function on_tick(ctx, dt_ms)
  local t = ctx.time_ms * 0.0002
  screen.fill_rect(0, 0, W, 68, 10, 12, 30)
  screen.fill_rect(0, 68, W, 67, 18, 8, 24)
  for i = 1, N do
    local p = P[i]
    local n = noise2d(p[1] * 0.012 + t, p[2] * 0.012)
    local a = n * 6.2832
    p[1] = p[1] + math.cos(a) * 1.5
    p[2] = p[2] + math.sin(a) * 1.5
    p[3] = p[3] + 1
    if p[1] < 0 then p[1] = p[1] + W elseif p[1] >= W then p[1] = p[1] - W end
    if p[2] < 0 then p[2] = p[2] + H elseif p[2] >= H then p[2] = p[2] - H end
    local k = p[3] * 0.003 % 1
    local r = 200 + math.floor(55 * k)
    local g = 60 + math.floor(120 * (1 - k))
    local b = 80 + math.floor(80 * k)
    screen.fill_rect(math.floor(p[1]), math.floor(p[2]), 2, 2, r, g, b)
  end
  screen.flip()
end
