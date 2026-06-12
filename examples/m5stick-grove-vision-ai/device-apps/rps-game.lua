-- rps-game.lua — rock/paper/scissors against the camera, best of three.
--
-- Practice mode (default): live monitor of what the gesture model sees,
-- press a button to start a match. Each round: countdown with beeps →
-- "SHOOT!" capture window (your gesture is voted over ~0.8s of frames) →
-- split-screen reveal, You vs Me, with the winner banner. Draws and missed
-- hands replay the round. First to 2 takes the match.
--
-- Assumes the SenseCraft rock-paper-scissors model: 0=paper, 1=rock,
-- 2=scissors (validate with rps-monitor.lua first).

local FRAME = 192

local LABELS = { [0] = "PAPER", [1] = "ROCK", [2] = "SCISSORS" }
local COLORS = {
  [0] = {80, 200, 255},   -- paper: cyan
  [1] = {255, 120, 60},   -- rock: orange
  [2] = {255, 220, 0},    -- scissors: yellow
}

local W, H = 240, 135  -- set properly in init

local function sx(v) return math.floor(v * W / FRAME) end
local function sy(v) return math.floor(v * H / FRAME) end
-- Mirror x so practice mode behaves like a mirror.
local function mx(v) return sx(FRAME - v) end

-- ---- state machine ---------------------------------------------------------
local state = "practice"   -- practice|countdown|capture|interstitial|reveal|matchover
local t = 0                -- ms in current state
local scores = { you = 0, me = 0 }
local your_pick, my_pick = nil, nil
local votes = {}
local msg = ""             -- interstitial text

local function go(s) state = s; t = 0 end

-- ---- melody queue (flourish: multi-note jingles via on_tick) ---------------
local tune = nil           -- { {at, freq, dur}, ... } sorted by at
local tune_t = 0

local function play(notes) tune = notes; tune_t = 0 end

local function tick_tune(dt)
  if not tune then return end
  local before = tune_t
  tune_t = tune_t + dt
  for i = 1, #tune do
    local n = tune[i]
    if n[1] >= before and n[1] < tune_t then buzzer.beep(n[2], n[3]) end
  end
  if tune_t > tune[#tune][1] + tune[#tune][3] then tune = nil end
end

local WIN_TUNE  = { {0, 523, 90}, {120, 659, 90}, {240, 784, 160} }
local LOSE_TUNE = { {0, 392, 90}, {120, 330, 90}, {240, 262, 200} }
local SHOOT_BEEP = { {0, 880, 120} }

-- ---- vision helpers --------------------------------------------------------
local function best_detection()
  if vision.kind() ~= "boxes" then return nil end
  local best = nil
  for i = 1, vision.count() do
    local d = vision.detection(i)
    if d and (best == nil or d.score > best.score) then best = d end
  end
  return best
end

local function beats(a, b)  -- does gesture a beat gesture b?
  return (a == 1 and b == 2)    -- rock crushes scissors
      or (a == 2 and b == 0)    -- scissors cut paper
      or (a == 0 and b == 1)    -- paper wraps rock
end

-- ---- drawing helpers -------------------------------------------------------
local function draw_pips(cx)
  -- match score as filled/hollow pips, centered-ish at top
  for i = 1, 2 do
    local fx = cx - 26 + (i - 1) * 14
    if scores.you >= i then screen.fill_rect(fx, 4, 8, 8, 0, 255, 0)
    else screen.rect(fx, 4, 8, 8, 0, 255, 0) end
    local gx = cx + 18 + (i - 1) * 14
    if scores.me >= i then screen.fill_rect(gx, 4, 8, 8, 255, 60, 60)
    else screen.rect(gx, 4, 8, 8, 255, 60, 60) end
  end
  screen.text(cx - 3, 3, "v", 1)
end

-- chunky pictograms, ~44px wide, centred on (cx, cy). Two-tone stacked fills
-- (outline triangles render unreliably on this panel — use fills only).
local function draw_icon(g, cx, cy)
  if g == 1 then  -- rock: boulder, dark base + light top
    screen.fill_rect(cx - 20, cy - 10, 40, 24, 110, 100, 95)
    screen.fill_rect(cx - 14, cy - 16, 28, 12, 150, 140, 130)
    screen.fill_rect(cx - 8, cy - 19, 14, 6, 180, 170, 160)
  elseif g == 0 then  -- paper: sheet with folded corner + text lines
    screen.fill_rect(cx - 14, cy - 18, 28, 36, 240, 240, 240)
    screen.fill_triangle(cx + 14, cy - 18, cx + 6, cy - 18, cx + 14, cy - 10,
                         180, 180, 180)
    for i = 0, 2 do
      screen.fill_rect(cx - 9, cy - 8 + i * 8, 18, 2, 150, 150, 150)
    end
  else  -- scissors: two crossed blades + handle blocks
    screen.fill_triangle(cx - 14, cy - 18, cx - 8, cy - 18, cx + 8, cy + 8,
                         210, 210, 220)
    screen.fill_triangle(cx + 14, cy - 18, cx + 8, cy - 18, cx - 8, cy + 8,
                         210, 210, 220)
    screen.fill_rect(cx - 16, cy + 8, 12, 10, 200, 60, 60)
    screen.fill_rect(cx + 4, cy + 8, 12, 10, 200, 60, 60)
  end
end

local function draw_half(x0, who, g, won, draw_bg)
  local half = math.floor(W / 2)
  local c = COLORS[g] or {120, 120, 120}
  if draw_bg then
    -- dimmed gesture colour as the background wash
    screen.fill_rect(x0, 0, half, H,
                     math.floor(c[1] / 3), math.floor(c[2] / 3),
                     math.floor(c[3] / 3))
  end
  screen.text(x0 + 8, 18, who, 2)
  draw_icon(g, x0 + math.floor(half / 2), 68)
  screen.text(x0 + 8, 95, LABELS[g] or "?", 2, c[1], c[2], c[3])
  if won then
    screen.fill_rect(x0 + 2, H - 18, half - 4, 16, 0, 160, 0)
    screen.text(x0 + math.floor(half / 2) - 36, H - 16, "WINNER", 2)
  end
end

-- ---- screens ---------------------------------------------------------------
local function draw_practice()
  screen.clear()
  if not vision.ok() then
    screen.text(10, 55, "camera offline", 2, 255, 80, 80)
    screen.flip()
    return
  end
  if vision.kind() ~= "boxes" and vision.kind() ~= "none" then
    screen.text(10, 40, "wrong model:", 2, 255, 80, 80)
    screen.text(10, 60, "flash rock/paper/scissors", 2, 255, 80, 80)
    screen.flip()
    return
  end

  screen.text(5, 3, "Practice mode", 2, 0, 200, 200)
  local best = best_detection()
  if best then
    local c = COLORS[best.target] or {255, 0, 255}
    local x = mx(best.x + best.w / 2)
    local y = sy(best.y - best.h / 2)
    screen.rect(x, y, sx(best.w), sy(best.h), c[1], c[2], c[3])
    screen.text(10, 35, LABELS[best.target] or ("target " .. best.target),
                4, c[1], c[2], c[3])
    screen.text(10, 75, "score " .. best.score, 2)
  else
    screen.text(10, 55, "show a hand...", 2, 120, 120, 120)
  end
  -- blinking start hint
  if (t % 1200) < 800 then
    screen.text(10, H - 18, "press button to start", 2, 0, 255, 0)
  end
  screen.flip()
end

local COUNT_WORDS = { "ROCK", "PAPER", "SCISSORS" }

local function draw_countdown()
  screen.clear()
  draw_pips(math.floor(W / 2))
  local step = math.floor(t / 700) + 1
  local word = COUNT_WORDS[step] or "..."
  screen.text(math.floor(W / 2) - #word * 12, 50, word, 4)
  -- live hand indicator so you know you're in frame before the shoot
  if best_detection() then
    screen.text(W - 70, H - 16, "hand ok", 2, 0, 255, 0)
  else
    screen.text(W - 70, H - 16, "no hand", 2, 120, 120, 120)
  end
  screen.flip()
end

local function draw_capture()
  screen.clear()
  draw_pips(math.floor(W / 2))
  screen.text(math.floor(W / 2) - 60, 45, "SHOOT", 4, 255, 255, 0)
  screen.flip()
end

local function draw_interstitial()
  screen.clear()
  draw_pips(math.floor(W / 2))
  screen.text(math.floor(W / 2) - #msg * 6, 55, msg, 2, 255, 220, 0)
  screen.flip()
end

local function draw_reveal()
  screen.clear()
  local you_won = beats(your_pick, my_pick)
  draw_half(0, "You", your_pick, you_won, true)
  draw_half(math.floor(W / 2), "Me", my_pick, not you_won, true)
  screen.line(math.floor(W / 2), 0, math.floor(W / 2), H, 0, 0, 0)
  draw_pips(math.floor(W / 2))
  screen.flip()
end

local function draw_matchover()
  screen.clear()
  local won = scores.you > scores.me
  if won then
    screen.fill_rect(0, 0, W, H, 0, 60, 0)
    screen.text(30, 35, "YOU WIN!", 4, 0, 255, 0)
  else
    screen.fill_rect(0, 0, W, H, 60, 0, 0)
    screen.text(45, 35, "I WIN!", 4, 255, 80, 80)
  end
  screen.text(85, 75, scores.you .. " - " .. scores.me, 3)
  if (t % 1200) < 800 then
    screen.text(35, H - 20, "press to play again", 2)
  end
  screen.flip()
end

-- ---- round flow ------------------------------------------------------------
local function start_countdown()
  votes = {}
  your_pick, my_pick = nil, nil
  go("countdown")
  play({ {0, 440, 80}, {700, 440, 80}, {1400, 440, 80} })
end

local function finish_capture()
  local top, top_votes = nil, 0
  for g, v in pairs(votes) do
    if v > top_votes then top, top_votes = g, v end
  end
  if top == nil then
    msg = "no hand seen - again!"
    go("interstitial")
    return
  end
  your_pick = top
  my_pick = math.random(0, 2)
  if your_pick == my_pick then
    msg = "draw - again!"
    go("interstitial")
    return
  end
  if beats(your_pick, my_pick) then
    scores.you = scores.you + 1
    play(WIN_TUNE)
  else
    scores.me = scores.me + 1
    play(LOSE_TUNE)
  end
  go("reveal")
end

-- ---- lifecycle -------------------------------------------------------------
function init(ctx)
  W = screen.width()
  H = screen.height()
  math.randomseed((ctx.time_ms or 0) + (ctx.trigger_count or 0) * 7919)
  go("practice")
  draw_practice()
end

function on_tick(ctx, dt)
  t = t + dt
  tick_tune(dt)

  if state == "practice" then
    draw_practice()
  elseif state == "countdown" then
    if t >= 2100 then
      go("capture")
      play(SHOOT_BEEP)
    end
    draw_countdown()
  elseif state == "capture" then
    local d = best_detection()
    if d then votes[d.target] = (votes[d.target] or 0) + 1 end
    if t >= 800 then finish_capture() end
    draw_capture()
  elseif state == "interstitial" then
    if t >= 1300 then start_countdown() end
    draw_interstitial()
  elseif state == "reveal" then
    draw_reveal()
  elseif state == "matchover" then
    draw_matchover()
  end
end

function on_event(ctx, e)
  if e.name ~= "button" then return end

  if state == "practice" then
    scores.you, scores.me = 0, 0
    start_countdown()
  elseif state == "reveal" then
    if scores.you >= 2 or scores.me >= 2 then
      go("matchover")
    else
      start_countdown()
    end
  elseif state == "matchover" then
    go("practice")
  end
end
