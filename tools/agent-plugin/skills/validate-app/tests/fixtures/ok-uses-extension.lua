function on_tick(ctx, dt_ms)
  local strength = wifi.signal_strength()
  local pct = strength / 100
  if wifi.is_connected() then
    local _ = pct
  end
end
