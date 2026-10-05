-- datetime's argument checks are the device's: month 13 must fail.
function init(ctx)
  local d = datetime.date(2026, 13, 1)
end
