-- Hardcoded stubs for the Resident sandbox built-in surface.
-- Loaded before the user's app so calls don't crash with nil-derefs.

log = {
  info  = function(_) end,
  warn  = function(_) end,
  error = function(_) end,
}

-- time: Python 3's time module shape (see the sandbox docs). Fixed values —
-- a synced clock reading Saturday 2026-10-03 12:00:00 UTC, zone UTC — so
-- apps that do arithmetic on the results run.
local STUB_EPOCH = 1791028800
local STUB_TM = {
  tm_year = 2026, tm_mon = 10, tm_mday = 3,
  tm_hour = 12, tm_min = 0, tm_sec = 0,
  tm_wday = 5, tm_yday = 276, tm_isdst = 0,
  tm_zone = "UTC", tm_gmtoff = 0,
}
local function stub_tm()
  local t = {}
  for k, v in pairs(STUB_TM) do t[k] = v end
  return t
end
local stub_ticks = 0

time = {
  time       = function() return STUB_EPOCH end,
  gmtime     = function(_) return stub_tm() end,
  localtime  = function(_) return stub_tm() end,
  mktime     = function(t)
    assert(type(t) == "table" and t.tm_year and t.tm_mon and t.tm_mday,
      "time.mktime: tm_year, tm_mon and tm_mday are required")
    return STUB_EPOCH
  end,
  strftime   = function(fmt, _)
    assert(type(fmt) == "string", "time.strftime: format must be a string")
    return fmt
  end,
  ticks_ms   = function() stub_ticks = stub_ticks + 100; return stub_ticks end,
  ticks_diff = function(a, b) return a - b end,
  synced     = function() return true end,
}

-- datetime: the device's own module — datetime.lua beside this file is a
-- verbatim copy of the source in Resident's src/ResidentDatetime.h, which
-- validate.sh runs over these primitives — so an app fails here exactly as
-- it would there. The clock is the same fixed, synced 2026-10-03 12:00:00
-- UTC as time's, in a zone that is UTC; strftime returns the format.
local function days_from_civil(y, m, d)
  y = m <= 2 and y - 1 or y
  local era = (y >= 0 and y or y - 399) // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  return era * 146097 + yoe * 365 + yoe // 4 - yoe // 100 + doy - 719468
end
local function civil_from_days(z)
  z = z + 719468
  local era = (z >= 0 and z or z - 146096) // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local m = mp < 10 and mp + 3 or mp - 9
  return yoe + era * 400 + (m <= 2 and 1 or 0), m, doy - (153 * mp + 2) // 5 + 1
end
local function stub_epoch(y, mo, d, h, mi, s)
  local e = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s
  if e < -2147483648 or e > 2147483647 then
    error("datetime: outside 1901-12-13..2038-01-19, the reach of 32-bit epoch seconds", 3)
  end
  return e
end
local DATETIME_PRIMITIVES = {
  now = function() return STUB_EPOCH end,
  split = function(secs)
    local y, m, d = civil_from_days(secs // 86400)
    local s = secs % 86400
    return y, m, d, s // 3600, s // 60 % 60, s % 60
  end,
  epoch = stub_epoch,
  resolve = function(...) return stub_epoch(...), 0, "UTC" end,
  ord = function(y, m, d) return days_from_civil(y, m, d) + 719163 end,
  civil = function(n) return civil_from_days(n - 719163) end,
  strftime = function(fmt) return fmt end,
  mul = function(d, s, n)
    local t = s * n
    return d * n + t // 86400, t % 86400
  end,
  synced = function() return true end,
  raise = function(msg) error(msg, 3) end,
}

-- kv may or may not be present on a given device, but apps that use it
-- need a non-crashing stub. Returns nil from get; true from set.
kv = {
  get = function(_) return nil end,
  set = function(_, _) return true end,
}

-- There are deliberately NO bare math globals (floor, sin, min, ...) and no
-- rgb/fract/beat/noise2d: the sandbox doesn't provide them, so an app that
-- calls one must fail here too. Apps use math.*.
