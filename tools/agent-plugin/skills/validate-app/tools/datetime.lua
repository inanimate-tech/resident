-- datetime: Python's datetime, Lua-shaped.
--
-- Every datetime is aware, in one of two zones: local (the one the sandbox
-- resolved, DST per instant) or UTC. Seconds are whole, as everywhere in
-- this VM (LUA_32BITS). Dates are proleptic-Gregorian ordinals over
-- 0001..9999, so date arithmetic never touches epoch seconds; only what must
-- (now, timestamp, a local offset, crossing zones) is int32, and the C side
-- raises past 2038-01-19 rather than wrapping.
--
-- Run on first touch with the C primitives and the table to fill: Sandbox
-- installs `datetime` as a stub that loads this into itself, so an app that
-- never uses it never pays for it.
local P, M = ...
local type, floor, tointeger = type, math.floor, math.tointeger
local getmetatable, setmetatable, rawequal, pairs, tostring =
      getmetatable, setmetatable, rawequal, pairs, tostring
local fmt, find = string.format, string.find
local ord, civil, raise = P.ord, P.civil, P.raise

local DATE, DT, TD, TZ = {}, {}, {}, {}
local DATEM, DTM, TDM = {}, {}, {}
DATE.__index, DT.__index, TD.__index = DATEM, DTM, TDM
TZ.__tostring = function(z) return z.name end
local UTC = setmetatable({ name = "UTC" }, TZ)
local LOCAL = setmetatable({ name = "local" }, TZ)

local KIND = { [DATE] = "date", [DT] = "datetime", [TD] = "timedelta", [TZ] = "timezone" }
local function kind(v)
  return KIND[getmetatable(v) or 0] or type(v)
end
local function whole(v)
  return type(v) == "number" and tointeger(v) or nil
end

-- Argument checks. raise() blames the app's line, however deep in here it
-- is called from.
local function dim(y, m)
  if m == 2 then return (y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)) and 29 or 28 end
  return (m == 4 or m == 6 or m == 9 or m == 11) and 30 or 31
end
local function ymd(who, y, m, d)
  local Y, Mo = whole(y), whole(m)
  if not Y or Y < 1 or Y > 9999 then raise(who .. ": year must be 1..9999") end
  if not Mo or Mo < 1 or Mo > 12 then raise(who .. ": month must be 1..12") end
  local n, D = dim(Y, Mo), whole(d)
  if not D or D < 1 or D > n then raise(fmt("%s: day must be 1..%d", who, n)) end
  return Y, Mo, D
end
local function hms(who, h, mi, s)
  local H, Mi, S = whole(h or 0), whole(mi or 0), whole(s or 0)
  if not H or H < 0 or H > 23 then raise(who .. ": hour must be 0..23") end
  if not Mi or Mi < 0 or Mi > 59 then raise(who .. ": minute must be 0..59") end
  if not S or S < 0 or S > 59 then raise(who .. ": second must be 0..59") end
  return H, Mi, S
end
local function zonearg(who, tz)
  if tz == nil then return LOCAL end
  if getmetatable(tz) ~= TZ then raise(who .. ": tz must be datetime.UTC, or nil for local") end
  return tz
end
local function keys(who, r, allowed)
  if r == nil then return {} end
  if type(r) ~= "table" then raise(who .. ": expected a table of fields") end
  for k in pairs(r) do
    if not allowed[k] then raise(fmt("%s: no field '%s'", who, tostring(k))) end
  end
  return r
end

local function mkdate(y, m, d)
  return setmetatable({ year = y, month = m, day = d }, DATE)
end
local function mkdt(y, m, d, h, mi, s, tz)
  return setmetatable({ year = y, month = m, day = d, hour = h, minute = mi, second = s,
                        tzinfo = tz }, DT)
end
-- Python's normal form: 0 <= seconds < 86400, the sign carried by days.
local function mktd(d, s)
  d, s = d + s // 86400, s % 86400
  if d < -999999999 or d > 999999999 then raise("datetime.timedelta: out of range") end
  return setmetatable({ days = d, seconds = s }, TD)
end
local function fromord(o)
  if o < 1 or o > 3652059 then raise("datetime: result out of range (years 1..9999)") end
  return civil(o)
end
local function sod(t) return t.hour * 3600 + t.minute * 60 + t.second end

-- The UTC offset (seconds east) and abbreviation. A local one goes through
-- the zone, so it needs the instant: int32.
local function zone(t)
  if t.tzinfo == UTC then return 0, "UTC" end
  local _, off, name = P.resolve(t.year, t.month, t.day, t.hour, t.minute, t.second)
  return off, name
end
local function at(secs, tz)
  local y, m, d, h, mi, s = P.split(secs, tz == LOCAL)
  return mkdt(y, m, d, h, mi, s, tz)
end
-- Wall-clock arithmetic, as Python's: the offset is re-resolved after, so
-- noon plus a day is noon the next day across a DST change.
local function shift(t, days, secs)
  local s = sod(t) + secs
  local y, m, d = fromord(ord(t.year, t.month, t.day) + days + s // 86400)
  s = s % 86400
  return mkdt(y, m, d, s // 3600, s // 60 % 60, s % 60, t.tzinfo)
end
-- Two datetimes as (ordinal, second-of-day) pairs: on the wall clock when
-- they share a zone (Python ignores the offset then), in UTC when not.
local function utc(t)
  local s = sod(t) - (zone(t))
  return ord(t.year, t.month, t.day) + s // 86400, s % 86400
end
local function dtkeys(a, b)
  if rawequal(a.tzinfo, b.tzinfo) then
    return ord(a.year, a.month, a.day), sod(a), ord(b.year, b.month, b.day), sod(b)
  end
  local ao, as = utc(a)
  local bo, bs = utc(b)
  return ao, as, bo, bs
end

-- Operators, shared by the three metatables and dispatched on kind.
local function add(a, b)
  local ka, kb = kind(a), kind(b)
  local x, td = a, b
  if ka == "timedelta" and kb ~= "timedelta" then x, td = b, a end
  if kind(td) == "timedelta" then
    local kx = kind(x)
    if kx == "timedelta" then return mktd(x.days + td.days, x.seconds + td.seconds) end
    if kx == "date" then return mkdate(fromord(ord(x.year, x.month, x.day) + td.days)) end
    if kx == "datetime" then return shift(x, td.days, td.seconds) end
  end
  raise(fmt("unsupported operand types for +: '%s' and '%s'", ka, kb))
end
local function sub(a, b)
  local ka, kb = kind(a), kind(b)
  if kb == "timedelta" then
    if ka == "timedelta" then return mktd(a.days - b.days, a.seconds - b.seconds) end
    if ka == "date" then return mkdate(fromord(ord(a.year, a.month, a.day) - b.days)) end
    if ka == "datetime" then return shift(a, -b.days, -b.seconds) end
  elseif ka == kb and ka == "date" then
    return mktd(ord(a.year, a.month, a.day) - ord(b.year, b.month, b.day), 0)
  elseif ka == kb and ka == "datetime" then
    local ao, as, bo, bs = dtkeys(a, b)
    return mktd(ao - bo, as - bs)
  end
  raise(fmt("unsupported operand types for -: '%s' and '%s'", ka, kb))
end
local function cmpkeys(a, b)
  local ka, kb = kind(a), kind(b)
  if ka ~= kb then raise(fmt("can't compare %s to %s", ka, kb)) end
  if ka == "date" then return ord(a.year, a.month, a.day), 0, ord(b.year, b.month, b.day), 0 end
  if ka == "timedelta" then return a.days, a.seconds, b.days, b.seconds end
  return dtkeys(a, b)
end
local function eq(a, b)
  if kind(a) ~= kind(b) then return false end   -- a date is never a datetime
  local a1, a2, b1, b2 = cmpkeys(a, b)
  return a1 == b1 and a2 == b2
end
local function lt(a, b)
  local a1, a2, b1, b2 = cmpkeys(a, b)
  return a1 < b1 or (a1 == b1 and a2 < b2)
end
local function le(a, b)
  local a1, a2, b1, b2 = cmpkeys(a, b)
  return a1 < b1 or (a1 == b1 and a2 <= b2)
end
for _, mt in pairs({ DATE, DT, TD }) do
  mt.__add, mt.__sub, mt.__eq, mt.__lt, mt.__le = add, sub, eq, lt, le
end

-- timedelta
TD.__unm = function(t) return mktd(-t.days, -t.seconds) end
TD.__mul = function(a, b)
  if kind(a) ~= "timedelta" then a, b = b, a end
  local n = whole(b)
  if not n then raise("timedelta * n: n must be an integer") end
  local d, s = P.mul(a.days, a.seconds, n)
  return mktd(d, s)
end
TD.__tostring = function(t)
  local s, d = t.seconds, t.days
  local clock = fmt("%d:%02d:%02d", s // 3600, s // 60 % 60, s % 60)
  if d == 0 then return clock end
  return fmt("%d day%s, %s", d, (d == 1 or d == -1) and "" or "s", clock)
end
function TDM.total_seconds(t)
  if t.days < -24855 or t.days > 24854 then
    raise("timedelta:total_seconds: more seconds than 32 bits hold (about 68 years)")
  end
  return t.days * 86400 + t.seconds
end

local UNITS = { weeks = 604800, days = 86400, hours = 3600, minutes = 60, seconds = 1 }
-- One field into (days, seconds) without overflowing: whole days first, and
-- a fraction rounded to the second.
local function accrue(d, s, k, v)
  if type(v) ~= "number" then raise(fmt("datetime.timedelta: %s must be a number", k)) end
  local unit = UNITS[k]
  local i = tointeger(v)
  if not i then
    i = tointeger(floor(v))
    if not i then raise(fmt("datetime.timedelta: %s out of range", k)) end
    s = s + floor((v - i) * unit + 0.5)
  end
  if unit >= 86400 then
    local per = unit // 86400
    if i > 999999999 // per or i < -999999999 // per then
      raise(fmt("datetime.timedelta: %s out of range", k))
    end
    d = d + i * per
  else
    local per = 86400 // unit
    d, s = d + i // per, s + i % per * unit
  end
  if d < -999999999 or d > 999999999 then raise("datetime.timedelta: out of range") end
  return d, s
end
local function timedelta(a, b)
  local d, s = 0, 0
  if type(a) == "table" then
    for k, v in pairs(a) do
      if not UNITS[k] then
        raise(fmt("datetime.timedelta: no field '%s' (weeks, days, hours, minutes, seconds)",
                  tostring(k)))
      end
      d, s = accrue(d, s, k, v)
    end
  elseif a ~= nil then
    d, s = accrue(d, s, "days", a)
    if b ~= nil then d, s = accrue(d, s, "seconds", b) end
  end
  return mktd(d, s)
end

-- date (and the half of it a datetime shares)
local function weekday(t) return (ord(t.year, t.month, t.day) + 6) % 7 end
local function isoweekday(t) return weekday(t) + 1 end
local function toordinal(t) return ord(t.year, t.month, t.day) end
local function checkfmt(who, f)
  if type(f) ~= "string" then raise(who .. ": format must be a string") end
end
DATE.__tostring = function(t) return fmt("%04d-%02d-%02d", t.year, t.month, t.day) end
DATEM.weekday, DATEM.isoweekday, DATEM.toordinal = weekday, isoweekday, toordinal
DATEM.isoformat = DATE.__tostring
function DATEM.strftime(t, f)
  checkfmt("date:strftime", f)
  return P.strftime(f, t.year, t.month, t.day, 0, 0, 0)
end
local DATE_FIELDS = { year = true, month = true, day = true }
function DATEM.replace(t, r)
  r = keys("date:replace", r, DATE_FIELDS)
  return mkdate(ymd("date:replace", r.year or t.year, r.month or t.month, r.day or t.day))
end

-- datetime
DT.__tostring = function(t)
  return fmt("%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second)
end
DTM.weekday, DTM.isoweekday, DTM.toordinal = weekday, isoweekday, toordinal
function DTM.date(t) return mkdate(t.year, t.month, t.day) end
function DTM.timestamp(t)
  if t.tzinfo == UTC then return P.epoch(t.year, t.month, t.day, t.hour, t.minute, t.second) end
  return (P.resolve(t.year, t.month, t.day, t.hour, t.minute, t.second))
end
function DTM.utcoffset(t) return mktd(0, (zone(t))) end
function DTM.tzname(t)
  local _, name = zone(t)
  return name
end
function DTM.astimezone(t, tz)
  tz = zonearg("datetime:astimezone", tz)
  if rawequal(tz, t.tzinfo) then return t end
  return at(DTM.timestamp(t), tz)
end
function DTM.strftime(t, f)
  checkfmt("datetime:strftime", f)
  local off, name
  if find(f, "%%[zZ]") then off, name = zone(t) end   -- only %z/%Z need the instant
  return P.strftime(f, t.year, t.month, t.day, t.hour, t.minute, t.second, off or 0, name or "")
end
function DTM.isoformat(t, sep)
  local off = zone(t)
  local sign = off < 0 and "-" or "+"
  if off < 0 then off = -off end
  return fmt("%04d-%02d-%02d%s%02d:%02d:%02d%s%02d:%02d", t.year, t.month, t.day, sep or "T",
             t.hour, t.minute, t.second, sign, off // 3600, off // 60 % 60)
end
local DT_FIELDS = { year = true, month = true, day = true, hour = true, minute = true,
                    second = true, tzinfo = true }
function DTM.replace(t, r)
  r = keys("datetime:replace", r, DT_FIELDS)
  local y, m, d = ymd("datetime:replace", r.year or t.year, r.month or t.month, r.day or t.day)
  local h, mi, s = hms("datetime:replace", r.hour or t.hour, r.minute or t.minute,
                       r.second or t.second)
  local tz = t.tzinfo
  if r.tzinfo ~= nil then tz = zonearg("datetime:replace", r.tzinfo) end
  return mkdt(y, m, d, h, mi, s, tz)
end

-- The module. It is callable, and is also its own `datetime.datetime`, so
-- datetime(...), datetime.datetime(...) and datetime.datetime.now() all work.
local date = setmetatable({}, { __call = function(_, y, m, d)
  return mkdate(ymd("datetime.date", y, m, d))
end })
function date.today() return M.today() end
function date.fromordinal(n)
  local o = whole(n)
  if not o or o < 1 or o > 3652059 then raise("datetime.date.fromordinal: n must be 1..3652059") end
  return mkdate(civil(o))
end

M.UTC = UTC
M.timezone = { utc = UTC }
M.datetime = M
M.date = date
M.timedelta = timedelta
M.synced = P.synced
function M.now(tz) return at(P.now(), zonearg("datetime.now", tz)) end
function M.today()
  local t = at(P.now(), LOCAL)
  return mkdate(t.year, t.month, t.day)
end
function M.fromtimestamp(secs, tz)
  if type(secs) ~= "number" then raise("datetime.fromtimestamp: secs must be a number") end
  return at(floor(secs), zonearg("datetime.fromtimestamp", tz))
end
setmetatable(M, { __call = function(_, y, m, d, h, mi, s, tz)
  y, m, d = ymd("datetime", y, m, d)
  h, mi, s = hms("datetime", h, mi, s)
  return mkdt(y, m, d, h, mi, s, zonearg("datetime", tz))
end })
