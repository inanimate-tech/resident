-- arc — L1 runtime for generative device UI on the Resident sandbox.
-- Spike / toy implementation. See DESIGN.md.
--
-- This file is concatenated ahead of a generation (the agent- or
-- human-written app) and pushed as one Resident app. It defines the
-- sandbox lifecycle globals (init / on_tick / on_event) itself;
-- generations only call arc.* / ui.* and never define those.
--
-- Written against Lua 5.1+ (device interpreter and host sim both work).

arc = arc or {}   -- the server may prepend `arc = { _gen = "<id>" }`
ui  = {}

arc._gen = arc._gen or "dev"

-- ---------------------------------------------------------------------------
-- internals

local now = 0                  -- ctx.time_ms, refreshed on every callback
local dirty = true             -- view needs re-render
local store_raw = nil          -- backing table of the store proxy
local store_dirty = false      -- mirror needs (throttled) publish
local view_fn = nil
local handlers = {}            -- intent name -> fn
local running = {}             -- coroutine -> true while it is a live handler
local timers = {}              -- list of {at, every, fn, last, dead}
local taps = {}                -- button index -> intent name
local shakes = {}              -- list of {g, name, until_ms}
local idles = {}               -- list of {ms, name, fired}
local last_activity = 0
local pending = {}             -- ask id -> {co, name, deadline, fallback}
local next_ask_id = 1
local outbox = {}              -- list of {name, data, keep}
local OUTBOX_MAX = 16
local STATE_MIN_INTERVAL = 5000
local last_state_sent = -STATE_MIN_INTERVAL

local unpack_ = table.unpack or unpack

local function flatten(t)
  -- events.send accepts flat string/number values only; booleans -> 1/0.
  -- Its serializer does NOT escape JSON specials, so a quote or backslash in
  -- a value corrupts the payload (it arrives as {}) — sanitize them away.
  local out = {}
  if t then
    for k, v in pairs(t) do
      local tv = type(v)
      if tv == "string" then out[k] = v:gsub('"', "'"):gsub("\\", "/")
      elseif tv == "number" then out[k] = v
      elseif tv == "boolean" then out[k] = v and 1 or 0 end
    end
  end
  return out
end

local function merged(base, extra)
  local out = flatten(extra)
  for k, v in pairs(base) do out[k] = v end   -- reserved keys win
  return out
end

local function outbox_push(name, data, keep)
  if #outbox >= OUTBOX_MAX then
    for i, m in ipairs(outbox) do            -- drop oldest droppable, never asks
      if not m.keep then table.remove(outbox, i); break end
    end
    if #outbox >= OUTBOX_MAX then return end -- all keepers: drop this one
  end
  outbox[#outbox + 1] = { name = name, data = data, keep = keep }
end

local function outbox_drain()
  while outbox[1] do
    if events.send(outbox[1].name, outbox[1].data) then
      table.remove(outbox, 1)
    else
      return  -- rate-limited or offline; retry next tick
    end
  end
end

local function resume(co, ...)
  local ok, err = coroutine.resume(co, ...)
  if not ok then log.error("arc handler: " .. tostring(err)) end
  if coroutine.status(co) == "dead" then running[co] = nil end
  dirty = true
end

-- ---------------------------------------------------------------------------
-- store

function arc.store(defaults)
  if store_raw then error("arc.store: only one store per app", 2) end
  store_raw = {}
  for k, v in pairs(defaults or {}) do store_raw[k] = v end
  arc._raw_store = store_raw   -- debug/sim access
  return setmetatable({}, {
    __index = store_raw,
    __newindex = function(_, k, v)
      if store_raw[k] ~= v then
        store_raw[k] = v
        dirty = true
        store_dirty = true
      end
    end,
  })
end

-- ---------------------------------------------------------------------------
-- intent bus

function arc.on(name, fn) handlers[name] = fn end

local function dispatch(name, payload, is_activity)
  if is_activity then
    last_activity = now
    for _, w in ipairs(idles) do w.fired = false end
  end
  outbox_push("arc:intent", merged({ name = name }, payload))
  local fn = handlers[name]
  if fn then
    local co = coroutine.create(fn)
    running[co] = true
    resume(co, payload or {})
  end
  dirty = true
end

function arc.intent(name, payload) dispatch(name, payload, true) end

-- recognizers -----------------------------------------------------------------

function arc.tap(index, name) taps[index] = name end

function arc.shake(g, name)
  shakes[#shakes + 1] = { g = g, name = name, until_ms = 0 }
end

function arc.idle(ms, name)
  idles[#idles + 1] = { ms = ms, name = name, fired = false }
end

-- asks ------------------------------------------------------------------------

function arc.ask(name, payload, opts)
  opts = opts or {}
  local co = coroutine.running()
  if not co or not running[co] then
    error("arc.ask must be called inside an intent handler", 2)
  end
  local id = next_ask_id
  next_ask_id = next_ask_id + 1
  pending[id] = {
    co = co, name = name,
    deadline = now + (opts.timeout_ms or 10000),
    fallback = opts.fallback,
    payload = payload,
  }
  outbox_push("arc:ask",
    merged({ ask = name, id = id, gen = arc._gen }, payload), true)
  dirty = true                       -- render the pending state immediately
  return coroutine.yield()
end

-- Returns the pending ask's PAYLOAD (truthy table) rather than a bare true,
-- so the view can echo what was asked during the wait — e.g. highlight the
-- choice the user just made. Falsy when nothing is pending.
function arc.pending(name)
  for _, p in pairs(pending) do
    if name == nil or p.name == name then return p.payload or true end
  end
  return false
end

-- timelines -------------------------------------------------------------------

function arc.every(ms, fn)
  local t = { at = now + ms, every = ms, fn = fn, last = now }
  timers[#timers + 1] = t
  return t
end

function arc.after(ms, fn)
  local t = { at = now + ms, fn = fn, last = now }
  timers[#timers + 1] = t
  return t
end

function arc.cancel(t) if t then t.dead = true end end

function arc.tween(o)
  local from, to, ms = o.from or 0, o.to or 1, o.ms or 300
  local ease, t0 = o.ease or "linear", now
  return function()
    local p = ms > 0 and (now - t0) / ms or 1
    if p < 0 then p = 0 elseif p > 1 then p = 1 end
    if ease == "in" then p = p * p
    elseif ease == "out" then p = 1 - (1 - p) * (1 - p) end
    return from + (to - from) * p
  end
end

function arc.play(notes)
  local at = 0
  for _, n in ipairs(notes) do
    local freq, ms = n[1], n[2]
    if freq > 0 then
      if at == 0 then buzzer.beep(freq, ms)
      else arc.after(at, function() buzzer.beep(freq, ms) end) end
    end
    at = at + ms
  end
end

-- misc ------------------------------------------------------------------------

function arc.publish(name, payload) outbox_push(name, flatten(payload)) end

function arc.view(fn) view_fn = fn end

function arc.fmt_ms(ms)
  local m = math.floor(ms / 60000)
  local s = math.floor(ms / 1000) % 60
  local t = math.floor(ms / 100) % 10
  return string.format("%02d:%02d.%d", m, s, t)
end

function arc.dots(n) return 1 + math.floor(now / 400) % n end

-- ---------------------------------------------------------------------------
-- ui: widgets are plain tables; the renderer below is deliberately dumb

local PALETTE = {
  white = {255,255,255}, black = {0,0,0},     gray = {160,160,160},
  dim   = {90,90,90},    red   = {255,80,80}, green = {80,255,120},
  blue  = {80,160,255},  amber = {255,190,60}, cyan = {80,220,255},
  magenta = {255,80,255},
}

local function rgbc(c)
  if type(c) == "string" then c = PALETTE[c] end
  c = c or PALETTE.white
  return c[1], c[2], c[3]
end

local function widget(kind)
  return function(t) t.kind = kind; return t end
end

ui.col    = widget("col")
ui.row    = widget("row")
ui.label  = widget("label")
ui.text   = widget("text")
ui.rect   = widget("rect")
ui.bar    = widget("bar")
ui.space  = widget("space")
ui.qr     = widget("qr")
ui.canvas = widget("canvas")

local CHAR_W, CHAR_H = 6, 8    -- base font cell; multiplied by size

local function wrap_text(str, size, w)
  local maxc = math.max(1, math.floor(w / (CHAR_W * size)))
  local lines, line = {}, ""
  for w in tostring(str):gmatch("%S+") do
    local word = w
    while #word > maxc do                      -- hard-split oversized words
      if #line > 0 then lines[#lines + 1] = line; line = "" end
      lines[#lines + 1] = word:sub(1, maxc)
      word = word:sub(maxc + 1)
    end
    if line == "" then line = word
    elseif #line + 1 + #word <= maxc then line = line .. " " .. word
    else lines[#lines + 1] = line; line = word end
  end
  if #line > 0 then lines[#lines + 1] = line end
  if #lines == 0 then lines[1] = "" end
  return lines
end

local measure

local function measure_children(w)
  local ws, hs, kids = {}, {}, {}
  for _, child in ipairs(w) do
    if type(child) == "table" then             -- skip false/holes from conds
      local cw, ch = measure(child)
      kids[#kids + 1] = child
      ws[#ws + 1] = cw
      hs[#hs + 1] = ch
    end
  end
  return kids, ws, hs
end

measure = function(w)
  local k = w.kind
  if k == "label" then
    local size = w.size or 2
    return #tostring(w.text or "") * CHAR_W * size, CHAR_H * size
  elseif k == "text" then
    local size = w.size or 2
    w._lines = wrap_text(w.text or "", size, w.w or 228)
    local widest = 0
    for _, l in ipairs(w._lines) do widest = math.max(widest, #l) end
    return widest * CHAR_W * size, #w._lines * CHAR_H * size
  elseif k == "rect" or k == "canvas" or k == "space" or k == "bar" then
    return w.w or 0, w.h or 0
  elseif k == "qr" then
    return 33 * (w.scale or 4), 33 * (w.scale or 4)
  elseif k == "col" or k == "row" then
    local pad, gap = w.pad or 0, w.gap or 0
    local kids, ws, hs = measure_children(w)
    w._kids, w._ws, w._hs = kids, ws, hs
    local main, cross = 0, 0
    for i = 1, #kids do
      if k == "col" then
        main = main + hs[i]; cross = math.max(cross, ws[i])
      else
        main = main + ws[i]; cross = math.max(cross, hs[i])
      end
    end
    if #kids > 0 then main = main + gap * (#kids - 1) end
    if k == "col" then return cross + 2 * pad, main + 2 * pad
    else return main + 2 * pad, cross + 2 * pad end
  end
  return 0, 0
end

local draw

local function draw_stack(w, x, y)
  local pad, gap = w.pad or 0, w.gap or 0
  local ww, wh = measure(w)  -- refresh _kids/_ws/_hs (cheap, tree is tiny)
  local cx, cy = x + pad, y + pad
  for i, child in ipairs(w._kids) do
    local dx, dy = cx, cy
    if w.align == "center" then
      if w.kind == "col" then dx = x + math.floor((ww - w._ws[i]) / 2)
      else dy = y + math.floor((wh - w._hs[i]) / 2) end
    end
    draw(child, dx, dy)
    if w.kind == "col" then cy = cy + w._hs[i] + gap
    else cx = cx + w._ws[i] + gap end
  end
end

draw = function(w, x, y)
  x, y = math.floor(x), math.floor(y)
  local k = w.kind
  if k == "label" then
    local r, g, b = rgbc(w.color)
    screen.text(x, y, tostring(w.text or ""), w.size or 2, r, g, b)
  elseif k == "text" then
    local size = w.size or 2
    local r, g, b = rgbc(w.color)
    for i, line in ipairs(w._lines or wrap_text(w.text or "", size, w.w or 228)) do
      screen.text(x, y + (i - 1) * CHAR_H * size, line, size, r, g, b)
    end
  elseif k == "rect" then
    local r, g, b = rgbc(w.color)
    if w.fill == false then screen.rect(x, y, w.w, w.h, r, g, b)
    else screen.fill_rect(x, y, w.w, w.h, r, g, b) end
  elseif k == "bar" then
    local r, g, b = rgbc(w.color)
    local v = math.max(0, math.min(1, w.value or 0))
    screen.rect(x, y, w.w, w.h, r, g, b)
    local fw = math.floor((w.w - 4) * v)
    if fw > 0 then screen.fill_rect(x + 2, y + 2, fw, w.h - 4, r, g, b) end
  elseif k == "qr" then
    screen.qr(x, y, tostring(w.data or ""), w.scale or 4)
  elseif k == "canvas" then
    if w.draw then w.draw(x, y) end
  elseif k == "col" or k == "row" then
    draw_stack(w, x, y)
  end
end

local activity_on = true
function arc.activity(on) activity_on = (on ~= false) end

-- System chrome: whenever an ask is in flight, the runtime itself draws a
-- small pulsing indicator (three amber dots, bottom-right). Views can add
-- richer treatments on top, but silently running inference is not the
-- default — an app must opt out (arc.activity(false)) to hide it.
local function draw_activity()
  if not activity_on or next(pending) == nil then return end
  local w, h = screen.width(), screen.height()
  local lit = 1 + math.floor(now / 300) % 3
  for i = 1, 3 do
    local x = w - 24 + (i - 1) * 7
    if i == lit then screen.fill_rect(x, h - 7, 4, 4, 255, 190, 60)
    else screen.fill_rect(x, h - 6, 4, 2, 90, 70, 25) end
  end
end

local function render()
  if not view_fn then return end
  -- the whole pipeline is protected: a bad view (e.g. a model-written one)
  -- must never take the app down — it logs and leaves the last frame up
  local ok, err = pcall(function()
    local tree = view_fn()
    arc._last_tree = tree      -- debug/sim access; also the mirror's view
    screen.clear()
    draw(tree, 0, 0)
    draw_activity()
    screen.flip()
  end)
  if not ok then log.error("arc view: " .. tostring(err)); return end
  dirty = false
end

-- ---------------------------------------------------------------------------
-- mirror

local function send_state(force)
  if not store_raw then return end
  if not force and now - last_state_sent < STATE_MIN_INTERVAL then return end
  -- events.send serializes into a bounded 256-byte buffer; trim long strings
  -- so the mirror arrives intact rather than truncated mid-JSON
  local snap = merged({ gen = arc._gen }, store_raw)
  for k, v in pairs(snap) do
    if type(v) == "string" and #v > 40 then snap[k] = v:sub(1, 37) .. "..." end
  end
  outbox_push("arc:state", snap)
  last_state_sent = now
  store_dirty = false
end

-- ---------------------------------------------------------------------------
-- sandbox lifecycle (the only place these globals are defined)

function init(ctx)
  now = ctx.time_ms or 0
  last_activity = now
  send_state(true)
  outbox_drain()
  render()
end

function on_tick(ctx, dt_ms)
  now = ctx.time_ms

  -- timers (guard against timers added during iteration: snapshot length)
  local n = #timers
  for i = 1, n do
    local t = timers[i]
    if not t.dead and now >= t.at then
      if t.every then
        t.at = now + t.every
        local dt = now - t.last
        t.last = now
        t.fn(dt)
      else
        t.dead = true
        t.fn()
      end
    end
  end
  for i = #timers, 1, -1 do
    if timers[i].dead then table.remove(timers, i) end
  end

  -- shake recognizer
  if #shakes > 0 then
    local ax, ay, az = imu.accel()
    local mag = math.sqrt(ax * ax + ay * ay + az * az)
    for _, sh in ipairs(shakes) do
      if mag > sh.g and now >= sh.until_ms then
        sh.until_ms = now + 1000
        dispatch(sh.name, { mag = math.floor(mag * 100) / 100 }, true)
      end
    end
  end

  -- idle recognizer (an idle intent is not user activity: no resets)
  for _, w in ipairs(idles) do
    if not w.fired and now - last_activity >= w.ms then
      w.fired = true
      dispatch(w.name, { quiet_ms = now - last_activity }, false)
    end
  end

  -- ask timeouts
  for id, p in pairs(pending) do
    if now >= p.deadline then
      pending[id] = nil
      log.warn("arc ask '" .. p.name .. "' timed out; using fallback")
      resume(p.co, p.fallback or {})
    end
  end

  if store_dirty then send_state(false) end
  outbox_drain()

  render()
end

function on_event(ctx, e)
  -- note: e.ts_ms is millis-since-boot, a different epoch from ctx.time_ms;
  -- `now` stays at the last tick's value (≤100ms stale, fine for a reflex)
  if e.name == "arc:reply" then
    local d = e.data or {}
    local p = d.id and pending[d.id]
    if p then
      pending[d.id] = nil
      resume(p.co, d)
    else
      log.warn("arc: reply for unknown ask id " .. tostring(d.id))
    end
  elseif e.name == "arc:hydrate" then
    if store_raw then
      for k, v in pairs(e.data or {}) do
        store_raw[k] = v
      end
      dirty = true; store_dirty = true
    end
  elseif e.name == "arc:probe" then
    send_state(true)
  elseif e.name == "button" then
    local name = taps[e.index]
    if name then arc.intent(name, { index = e.index }) end
  else
    arc.intent(e.name, e.data)
  end
  if dirty then render() end   -- input→pixel inside the 150ms budget
  outbox_drain()               -- asks shouldn't wait for the next tick
end

-- end of arc runtime --------------------------------------------------------
