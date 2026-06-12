-- rps-monitor.lua — show what the gesture model is seeing (validation aid).
-- Draws every detection's box (mirrored, grey), highlights the best one in
-- its gesture colour, and prints the label BIG. The raw target number is
-- shown alongside so a wrong label-table assumption is immediately obvious —
-- cross-check against the model info JSON in the serial log.
-- Works with any boxes-kind model; unknown targets show as "target N".

local FRAME = 192

local LABELS = { [0] = "PAPER", [1] = "ROCK", [2] = "SCISSORS" }
local COLORS = {
  [0] = {80, 200, 255},   -- paper: cyan
  [1] = {255, 120, 60},   -- rock: orange
  [2] = {255, 220, 0},    -- scissors: yellow
}

local function sx(v) return math.floor(v * screen.width() / FRAME) end
local function sy(v) return math.floor(v * screen.height() / FRAME) end
-- Mirror x so the screen behaves like a mirror.
local function mx(v) return sx(FRAME - v) end

local function draw_box(d, r, g, b)
  -- d.x/d.y is the box centre in the model frame; mirrored, the left screen
  -- edge comes from the mirrored centre.
  local x = mx(d.x + d.w / 2)
  local y = sy(d.y - d.h / 2)
  screen.rect(x, y, sx(d.w), sy(d.h), r, g, b)
end

function init(ctx)
  screen.clear()
  screen.text(10, 10, "RPS monitor", 3)
  screen.flip()
end

function on_tick(ctx, dt)
  screen.clear()

  if not vision.ok() then
    screen.text(10, 50, "camera offline", 2, 255, 80, 80)
    screen.flip()
    return
  end

  local kind = vision.kind()
  local n = vision.count()
  screen.text(5, 3, kind .. " n=" .. n .. " age=" .. vision.age_ms() .. "ms",
              2, 120, 120, 120)

  if kind ~= "boxes" or n == 0 then
    screen.text(10, 60, "show a hand...", 2, 120, 120, 120)
    screen.flip()
    return
  end

  -- detection(i) is in frame order, not score order: scan for the best.
  local best = nil
  for i = 1, n do
    local d = vision.detection(i)
    if d then
      draw_box(d, 100, 100, 100)
      if best == nil or d.score > best.score then best = d end
    end
  end

  if best then
    local label = LABELS[best.target] or ("target " .. best.target)
    local c = COLORS[best.target] or {255, 0, 255}
    draw_box(best, c[1], c[2], c[3])
    screen.text(10, 35, label, 4, c[1], c[2], c[3])
    screen.text(10, 75, "t=" .. best.target .. "  score " .. best.score, 2)
  end

  screen.flip()
end
