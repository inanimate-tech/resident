-- Bounce: a ball bouncing around the screen
local x, y = 60, 40
local vx, vy = 80, 60
local radius = 8

function on_tick(ctx, dt_ms)
  local dt = dt_ms / 1000
  local w = screen.width()
  local h = screen.height()

  x = x + vx * dt
  y = y + vy * dt

  if x - radius < 0 then x = radius; vx = -vx end
  if x + radius > w then x = w - radius; vx = -vx end
  if y - radius < 0 then y = radius; vy = -vy end
  if y + radius > h then y = h - radius; vy = -vy end

  screen.clear(0, 0, 0)
  screen.fill_rect(
    math.floor(x - radius),
    math.floor(y - radius),
    radius * 2,
    radius * 2,
    255, 100, 50
  )
  screen.flip()
end
