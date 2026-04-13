-- Accel: visualize accelerometer data as a moving dot
function on_tick(ctx, dt_ms)
  local ax, ay, az = imu.accel()
  local w = screen.width()
  local h = screen.height()

  -- Map accelerometer to screen position (centered)
  local cx = math.floor(w / 2 + ax * w / 2)
  local cy = math.floor(h / 2 + ay * h / 2)

  -- Clamp to screen
  cx = math.max(4, math.min(w - 4, cx))
  cy = math.max(4, math.min(h - 4, cy))

  screen.clear(0, 0, 20)
  -- Crosshair
  screen.fill_rect(w / 2 - 1, 0, 2, h, 30, 30, 60)
  screen.fill_rect(0, h / 2 - 1, w, 2, 30, 30, 60)
  -- Dot
  screen.fill_rect(cx - 4, cy - 4, 8, 8, 0, 255, 100)
  screen.flip()
end
