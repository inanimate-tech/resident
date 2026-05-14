local notes = {
  {523, 600}, {440, 300}, {349, 700},
  {523, 600}, {440, 300}, {349, 700},
  {392, 250}, {440, 250}, {392, 250}, {349, 250}, {330, 250}, {294, 250}, {262, 750},
  {294, 400}, {330, 400}, {349, 600},
  {294, 400}, {330, 400}, {349, 600},
  {0, 1500},
}

local idx = 1
local next_at = 0

local function draw()
  screen.clear()
  screen.text(60, 20, "DAISY", 4, 255, 200, 0)
  screen.text(75, 65, "DAISY", 3, 255, 100, 200)
  local bw = math.floor(220 * (idx - 1) / #notes)
  screen.fill_rect(10, 120, 220, 6, 30, 30, 30)
  screen.fill_rect(10, 120, bw, 6, 0, 200, 255)
  screen.flip()
end

function init(ctx)
  draw()
end

function on_tick(ctx, dt_ms)
  if ctx.time_ms >= next_at then
    local n = notes[idx]
    if n[1] > 0 then buzzer.beep(n[1], n[2]) end
    next_at = ctx.time_ms + n[2]
    idx = idx + 1
    if idx > #notes then idx = 1 end
    draw()
  end
end
