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
    p[1] = p[1] + cos(a) * 1.5
    p[2] = p[2] + sin(a) * 1.5
    p[3] = p[3] + 1
    if p[1] < 0 then p[1] = p[1] + W elseif p[1] >= W then p[1] = p[1] - W end
    if p[2] < 0 then p[2] = p[2] + H elseif p[2] >= H then p[2] = p[2] - H end
    local k = fract(p[3] * 0.003)
    local r = 200 + math.floor(55 * k)
    local g = 60 + math.floor(120 * (1 - k))
    local b = 80 + math.floor(80 * k)
    screen.fill_rect(math.floor(p[1]), math.floor(p[2]), 2, 2, r, g, b)
  end
  screen.flip()
end
