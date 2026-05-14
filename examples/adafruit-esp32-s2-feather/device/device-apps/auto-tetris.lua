local COLS, ROWS, CELL = 10, 20, 12
local OX = 7
local OY = 0

local P = {
  {c={0,220,220},  r={{{0,1},{1,1},{2,1},{3,1}},{{2,0},{2,1},{2,2},{2,3}},{{0,2},{1,2},{2,2},{3,2}},{{1,0},{1,1},{1,2},{1,3}}}},
  {c={230,200,0},  r={{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}},{{1,0},{2,0},{1,1},{2,1}}}},
  {c={170,40,220}, r={{{1,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{2,1},{1,2}},{{0,1},{1,1},{2,1},{1,2}},{{1,0},{0,1},{1,1},{1,2}}}},
  {c={0,210,40},   r={{{1,0},{2,0},{0,1},{1,1}},{{1,0},{1,1},{2,1},{2,2}},{{1,0},{2,0},{0,1},{1,1}},{{1,0},{1,1},{2,1},{2,2}}}},
  {c={220,30,30},  r={{{0,0},{1,0},{1,1},{2,1}},{{2,0},{1,1},{2,1},{1,2}},{{0,0},{1,0},{1,1},{2,1}},{{2,0},{1,1},{2,1},{1,2}}}},
  {c={50,80,230},  r={{{0,0},{0,1},{1,1},{2,1}},{{1,0},{2,0},{1,1},{1,2}},{{0,1},{1,1},{2,1},{2,2}},{{1,0},{1,1},{0,2},{1,2}}}},
  {c={230,130,0},  r={{{2,0},{0,1},{1,1},{2,1}},{{1,0},{1,1},{1,2},{2,2}},{{0,1},{1,1},{2,1},{0,2}},{{0,0},{1,0},{1,1},{1,2}}}},
}

local grid, bag, bn
local cur, rot, col, row, tro, tco
local state, lines
local ft, frows, oot
local seed = 1

local function rnd(n)
  seed = (seed * 1103515245 + 12345) % 2147483647
  return (seed % n) + 1
end

local function ng()
  local g = {}
  for r=1,ROWS do
    local rr = {}
    for c=1,COLS do rr[c] = 0 end
    g[r] = rr
  end
  return g
end

local function refill()
  bag = {1,2,3,4,5,6,7}
  for i=7,2,-1 do
    local j = rnd(i)
    bag[i], bag[j] = bag[j], bag[i]
  end
  bn = 7
end

local function np()
  if bn == 0 then refill() end
  local p = bag[bn]; bn = bn - 1
  return p
end

local function coll(p, ro, co, rw, g)
  local pr = P[p].r[ro]
  for i=1,4 do
    local cc = co + pr[i][1]
    local cr = rw + pr[i][2]
    if cc < 1 or cc > COLS or cr > ROWS then return true end
    if cr >= 1 and g[cr][cc] ~= 0 then return true end
  end
  return false
end

local function eval_grid(g)
  local h = {}
  for c=1,COLS do
    h[c] = 0
    for r=1,ROWS do
      if g[r][c] ~= 0 then h[c] = ROWS - r + 1; break end
    end
  end
  local agg = 0
  for c=1,COLS do agg = agg + h[c] end
  local holes = 0
  for c=1,COLS do
    local seen = false
    for r=1,ROWS do
      if g[r][c] ~= 0 then seen = true
      elseif seen then holes = holes + 1 end
    end
  end
  local bump = 0
  for c=1,COLS-1 do bump = bump + abs(h[c] - h[c+1]) end
  local max_h = 0
  for c=1,COLS do if h[c] > max_h then max_h = h[c] end end
  local ln = 0
  for r=1,ROWS do
    local full = true
    for c=1,COLS do if g[r][c] == 0 then full = false; break end end
    if full then ln = ln + 1 end
  end
  local line_score
  if ln == 4 then line_score = 8.0
  elseif ln == 0 then line_score = 0
  elseif max_h < 12 then line_score = -12.0
  else line_score = 0.76 * ln
  end
  return -0.51 * agg + line_score - 0.36 * holes - 0.18 * bump
end

local function pick()
  local best = -1e9
  tro, tco = rot, col
  for ro=1,4 do
    for co=-2,COLS+2 do
      if not coll(cur, ro, co, 0, grid) then
        local rw = 0
        while not coll(cur, ro, co, rw + 1, grid) do rw = rw + 1 end
        local sim = {}
        for i=1,ROWS do
          local rc = {}
          for j=1,COLS do rc[j] = grid[i][j] end
          sim[i] = rc
        end
        local pr = P[cur].r[ro]
        local ok = true
        for i=1,4 do
          local cc = co + pr[i][1]
          local cr = rw + pr[i][2]
          if cr >= 1 and cr <= ROWS and cc >= 1 and cc <= COLS then
            sim[cr][cc] = cur
          else ok = false; break end
        end
        if ok then
          local s = eval_grid(sim)
          if s > best then best = s; tro = ro; tco = co end
        end
      end
    end
  end
end

local function spawn()
  cur = np()
  rot, col, row = 1, 4, 0
  if coll(cur, rot, col, row, grid) then return false end
  pick()
  return true
end

local function lock_p()
  local pr = P[cur].r[rot]
  for i=1,4 do
    local cc = col + pr[i][1]
    local cr = row + pr[i][2]
    if cr >= 1 and cr <= ROWS and cc >= 1 and cc <= COLS then
      grid[cr][cc] = cur
    end
  end
end

local function full_rows()
  local lst = {}
  for r=1,ROWS do
    local full = true
    for c=1,COLS do if grid[r][c] == 0 then full = false; break end end
    if full then lst[#lst + 1] = r end
  end
  return lst
end

local function clear_marked()
  local mk = {}
  for i=1,#frows do mk[frows[i]] = true end
  local dst = ROWS
  for src=ROWS,1,-1 do
    if not mk[src] then
      if src ~= dst then
        for c=1,COLS do grid[dst][c] = grid[src][c] end
      end
      dst = dst - 1
    end
  end
  for r=dst,1,-1 do
    for c=1,COLS do grid[r][c] = 0 end
  end
end

local function step()
  if rot ~= tro then
    local nr = rot + 1; if nr > 4 then nr = 1 end
    if not coll(cur, nr, col, row, grid) then rot = nr end
  elseif col ~= tco then
    local d = tco > col and 1 or -1
    if not coll(cur, rot, col + d, row, grid) then col = col + d end
  end
  if not coll(cur, rot, col, row + 1, grid) then
    row = row + 1
    return false
  end
  return true
end

local function drawc(c, r, cr, cg, cb)
  screen.fill_rect(OX + (c - 1) * CELL, OY + (r - 1) * CELL, CELL - 1, CELL - 1, cr, cg, cb)
end

local function draw_grid_skip(hide)
  for r=1,ROWS do
    if not hide or not hide[r] then
      for c=1,COLS do
        local p = grid[r][c]
        if p ~= 0 then
          local cc = P[p].c
          drawc(c, r, cc[1], cc[2], cc[3])
        end
      end
    end
  end
end

local function draw_cur()
  if not cur then return end
  local pr = P[cur].r[rot]
  local cc = P[cur].c
  for i=1,4 do
    local x = col + pr[i][1]
    local y = row + pr[i][2]
    if y >= 1 and y <= ROWS and x >= 1 and x <= COLS then
      drawc(x, y, cc[1], cc[2], cc[3])
    end
  end
end

local function reset()
  grid = ng()
  lines = 0
  bn = 0
  cur = nil
  state = "play"
end

function init(ctx)
  led.set_brightness(20)
  led.off()
  reset()
end

function on_event(ctx, event)
  if event.name == "button" then
    seed = ((event.count or 1) * 7919 + ctx.time_ms) % 2147483647
    if seed <= 0 then seed = 1 end
    reset()
    led.off()
  end
end

function on_tick(ctx, dt_ms)
  screen.clear()

  if state == "play" then
    if not cur then
      if not spawn() then
        state = "over"
        oot = ctx.time_ms
      end
    else
      if step() then
        lock_p()
        frows = full_rows()
        if #frows > 0 then
          state = "flash"
          ft = 0
          led.set(255, 255, 255)
        else
          cur = nil
        end
      end
    end
    if state == "play" then
      draw_grid_skip(nil)
      draw_cur()
    end
  end

  if state == "flash" then
    local hide = nil
    if ft >= 6 then
      hide = {}
      for i=1,#frows do hide[frows[i]] = true end
    end
    draw_grid_skip(hide)
    local white = ft < 2 or (ft >= 4 and ft < 6)
    if white then
      for i=1,#frows do
        local r = frows[i]
        for c=1,COLS do drawc(c, r, 255, 255, 255) end
      end
    end
    ft = ft + 1
    if ft >= 8 then
      lines = lines + #frows
      clear_marked()
      cur = nil
      state = "play"
      led.off()
    end
  elseif state == "over" then
    led.set(255, 40, 40)
    local s = tostring(lines)
    local sz = 4
    local cw = 6 * sz
    local ch = 8 * sz
    local tw = #s * cw
    screen.text(floor((135 - tw) / 2), floor((240 - ch) / 2) - 16, s, sz, 255, 220, 0)
    local lbl = "LINES"
    local lw = #lbl * 6 * 2
    screen.text(floor((135 - lw) / 2), floor((240 - ch) / 2) + ch + 4, lbl, 2, 220, 220, 220)
    if ctx.time_ms - oot >= 2000 then
      reset()
      led.off()
    end
  end

  screen.flip()
end
