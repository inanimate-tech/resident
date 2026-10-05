-- Driver that runs after builtins + device stubs + user app are loaded.
-- The user app's globals (init, on_tick, on_event) are now in _G.

-- ctx carries only time_ms (and generation_id, nil here as on a direct
-- load); the wall clock is the time module's.
local ctx = {
  time_ms = 0,
}

if not (init or on_tick or on_event) then
  io.stderr:write("validate-app: FAIL: app defines none of init / on_tick / on_event\n")
  os.exit(1)
end

if init then
  local ok, err = pcall(init, ctx)
  if not ok then
    io.stderr:write("validate-app: FAIL: init: " .. tostring(err) .. "\n")
    os.exit(1)
  end
end

if on_tick then
  for _ = 1, 5 do
    ctx.time_ms = ctx.time_ms + 100
    local ok, err = pcall(on_tick, ctx, 100)
    if not ok then
      io.stderr:write("validate-app: FAIL: on_tick: " .. tostring(err) .. "\n")
      os.exit(1)
    end
  end
end

-- on_event is not invoked in v1 (would require synthesizing events).
io.stdout:write("validate-app: OK\n")
os.exit(0)
