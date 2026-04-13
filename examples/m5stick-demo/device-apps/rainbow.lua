-- Rainbow: animated color cycling across the display
function on_tick(ctx, dt_ms)
  local t = ctx.time_ms / 1000
  for x = 0, screen.width() - 1, 4 do
    local hue = (x / screen.width() + t * 0.3) % 1.0
    local r = math.floor((math.sin(hue * 6.2832) * 0.5 + 0.5) * 255)
    local g = math.floor((math.sin(hue * 6.2832 + 2.094) * 0.5 + 0.5) * 255)
    local b = math.floor((math.sin(hue * 6.2832 + 4.189) * 0.5 + 0.5) * 255)
    screen.fill_rect(x, 0, 4, screen.height(), r, g, b)
  end
  screen.flip()
end
