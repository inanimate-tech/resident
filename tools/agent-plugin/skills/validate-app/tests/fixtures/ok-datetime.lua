-- The wall clock through datetime: arithmetic, comparison, formatting.
function on_tick(ctx, dt_ms)
  if not datetime.synced() then return end
  local now = datetime.now()
  local label = now:strftime("%a %H:%M")
  local left = (datetime.date(2026, 12, 25) - datetime.today()).days
  local tomorrow = datetime.today() + datetime.timedelta{ days = 1 }
  assert(left == 83 and tomorrow > datetime.today() and now.hour == 12)
  assert(tostring(datetime(2026, 10, 3, 12) - datetime.timedelta{ hours = 1 }) == "2026-10-03 11:00:00")
end
