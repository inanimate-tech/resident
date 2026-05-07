local x, y = 0, 0
local dx, dy = 1, 1

function on_tick(ctx, dt_ms)
  x = x + dx
  y = y + dy
  local ax, ay, az = imu.accel()
  screen.clear()
  screen.flip()
end
