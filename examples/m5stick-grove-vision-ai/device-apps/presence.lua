-- presence.lua — beep and flash when the camera sees something.
-- Works with any detection/classification model. Event-driven: no polling.

local state = "waiting"   -- "waiting" | "seen"
local last_target = -1
local last_score = 0

local function draw()
  screen.clear()
  if state == "seen" then
    screen.fill_rect(0, 0, screen.width(), screen.height(), 0, 80, 0)
    screen.text(10, 15, "SEEN!", 4, 255, 255, 255)
    screen.text(10, 70, "target " .. last_target .. "  score " .. last_score, 2)
  else
    screen.text(10, 15, "Watching...", 3, 0, 200, 200)
    if not vision.ok() then
      screen.text(10, 70, "camera offline", 2, 255, 80, 80)
    end
  end
  screen.flip()
end

function init(ctx)
  draw()
end

function on_event(ctx, e)
  if e.name ~= "vision" then return end
  if e.kind == "link" then
    draw()
    return
  end
  if e.n and e.n > 0 then
    if state ~= "seen" then buzzer.beep(880, 80) end
    state = "seen"
    last_target = e.target or -1
    last_score = e.score or 0
  else
    state = "waiting"
  end
  draw()
end
