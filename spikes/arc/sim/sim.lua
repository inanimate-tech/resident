-- sim — host-side harness for arc generations.
-- Stubs the m5stick sandbox surface (per examples/m5stick-demo/DEVICE-SKILL.md),
-- drives the tick clock, injects events, captures upstream sends.
-- Run from spikes/arc/:  lua sim/scenarios.lua

local sim = {}

sim.time    = 0
sim.sent    = {}     -- captured events.send calls: {name=, data=}
sim.beeps   = {}     -- captured buzzer.beep calls: {freq, ms}
sim.logs    = {}     -- captured log lines: {level, msg}
sim.accel   = { 0, 0, 1 }
sim.send_ok = true   -- set false to simulate the rate limiter / offline
sim.flips   = 0
sim.drawops = {}     -- draw calls in the current frame (screen.clear resets)

local function copy(t)
  local out = {}
  for k, v in pairs(t or {}) do out[k] = v end
  return out
end

local function install_stubs()
  local function noop() end

  screen = setmetatable({
    width  = function() return 240 end,
    height = function() return 135 end,
    flip   = function() sim.flips = sim.flips + 1 end,
    clear  = function() sim.drawops = {} end,
    text   = function(x, y, str, size, r, g, b)
      sim.drawops[#sim.drawops + 1] =
        { op = "text", x = x, y = y, str = str, size = size or 2 }
    end,
    fill_rect = function(x, y, w, h)
      sim.drawops[#sim.drawops + 1] =
        { op = "fill_rect", x = x, y = y, w = w, h = h }
    end,
    rect = function(x, y, w, h)
      sim.drawops[#sim.drawops + 1] =
        { op = "rect", x = x, y = y, w = w, h = h }
    end,
  }, { __index = function() return noop end })

  imu = {
    accel = function() return sim.accel[1], sim.accel[2], sim.accel[3] end,
    gyro  = function() return 0, 0, 0 end,
    temp  = function() return 0 end,
  }

  buzzer = setmetatable({
    beep = function(freq, ms) sim.beeps[#sim.beeps + 1] = { freq, ms } end,
  }, { __index = function() return noop end })

  button = { press_count = function() return 0 end }

  events = {
    send = function(name, data)
      if not sim.send_ok then return false end
      sim.sent[#sim.sent + 1] = { name = name, data = copy(data) }
      return true
    end,
  }

  log = {
    info  = function(m) sim.logs[#sim.logs + 1] = { "info", m } end,
    warn  = function(m) sim.logs[#sim.logs + 1] = { "warn", m } end,
    error = function(m) sim.logs[#sim.logs + 1] = { "error", m } end,
  }
end

local function ctx()
  return { time_ms = sim.time, trigger_count = 0,
           utc_h = 12, utc_m = 0, localtime_h = 12, localtime_m = 0 }
end

function sim.load(app_path)
  sim.time, sim.sent, sim.beeps, sim.logs = 0, {}, {}, {}
  sim.accel, sim.send_ok, sim.flips = { 0, 0, 1 }, true, 0
  sim.drawops = {}
  install_stubs()
  arc = nil                       -- fresh runtime state on re-dofile
  dofile("arc.lua")
  dofile(app_path)
  init(ctx())
end

function sim.tick(n)
  for _ = 1, (n or 1) do
    sim.time = sim.time + 100
    on_tick(ctx(), 100)
  end
end

function sim.button(index)
  on_event(ctx(), { name = "button", index = index, from = "", ts_ms = sim.time })
end

function sim.appevent(type_, data)
  on_event(ctx(), { name = type_, data = data or {}, from = "server", ts_ms = sim.time })
end

-- assertions helpers ---------------------------------------------------------

function sim.texts()
  local out = {}
  local function walk(w)
    if type(w) ~= "table" then return end
    if w.kind == "label" or w.kind == "text" then
      out[#out + 1] = tostring(w.text or "")
    end
    for _, child in ipairs(w) do walk(child) end
  end
  walk(arc._last_tree)
  return table.concat(out, " | ")
end

function sim.last_sent(name)
  for i = #sim.sent, 1, -1 do
    if sim.sent[i].name == name then return sim.sent[i].data end
  end
  return nil
end

function sim.sent_count(name)
  local n = 0
  for _, m in ipairs(sim.sent) do
    if m.name == name then n = n + 1 end
  end
  return n
end

-- Returns nil if every draw op in the current frame is inside the screen
-- (and every text line fits its own width), else a description of the first
-- offender. The strictest cheap check we have on the layout engine.
function sim.offscreen()
  for _, d in ipairs(sim.drawops) do
    local w, h
    if d.op == "text" then
      w, h = #d.str * 6 * d.size, 8 * d.size
    else
      w, h = d.w, d.h
    end
    if d.x < 0 or d.y < 0 or d.x + w > 240 or d.y + h > 135 then
      return string.format("%s at (%d,%d) size %dx%d", d.op, d.x, d.y, w, h)
    end
  end
  return nil
end

function sim.beeped(freq)
  for _, b in ipairs(sim.beeps) do
    if b[1] == freq then return true end
  end
  return false
end

return sim
