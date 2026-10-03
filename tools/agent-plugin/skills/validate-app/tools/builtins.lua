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

-- kv may or may not be present on a given device, but apps that use it
-- need a non-crashing stub. Returns nil from get; true from set.
kv = {
  get = function(_) return nil end,
  set = function(_, _) return true end,
}

-- There are deliberately NO bare math globals (floor, sin, min, ...) and no
-- rgb/fract/beat/noise2d: the sandbox doesn't provide them, so an app that
-- calls one must fail here too. Apps use math.*.
