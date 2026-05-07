-- Hardcoded stubs for the Resident sandbox built-in surface.
-- Loaded before the user's app so calls don't crash with nil-derefs.

log = {
  info  = function(_) end,
  warn  = function(_) end,
  error = function(_) end,
}

time = {
  is_valid     = function() return true end,
  has_timezone = function() return false end,
  hour         = function() return 12 end,
  minute       = function() return 0 end,
  second       = function() return 0 end,
  day_id       = function() return 1 end,
}

-- kv may or may not be present on a given device, but apps that use it
-- need a non-crashing stub. Returns nil from get; true from set.
kv = {
  get = function(_) return nil end,
  set = function(_, _) return true end,
}

-- Shader-compatible globals (also valid in apps).
function rgb(_, _, _) return -1 end       -- negative integer = "this is a color"
function fract(x) return x - math.floor(x) end
function beat(bpm, t) return t / (60000 / bpm) end
function noise2d(_, _) return 0 end

-- Math globals registered without the math. prefix.
floor = math.floor
ceil  = math.ceil
abs   = math.abs
sin   = math.sin
cos   = math.cos
tan   = math.tan
sqrt  = math.sqrt
min   = math.min
max   = math.max
fmod  = math.fmod
