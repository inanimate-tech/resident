-- tracker.lua — draw the best detection's box live on the LCD.
-- Works with boxes and pose kinds. Polls vision.* each tick.

-- Model-frame size. SenseCraft detection models typically infer on 192x192;
-- eyeball the serial log (x/y/w/h ranges) and adjust if your model differs.
local FRAME = 192

local function sx(v) return math.floor(v * screen.width() / FRAME) end
local function sy(v) return math.floor(v * screen.height() / FRAME) end
-- Mirror x: the camera faces you, so flip horizontally to make the screen
-- behave like a mirror (move left, box moves left).
local function mx(v) return sx(FRAME - v) end

function init(ctx)
  screen.clear()
  screen.text(10, 10, "Tracker", 3)
  screen.flip()
end

function on_tick(ctx, dt)
  screen.clear()
  local kind = vision.kind()
  local d = vision.detection(1)
  if d ~= nil and (kind == "boxes" or kind == "pose") then
    -- box is centered on (x, y) in model frame; mirrored, the left screen
    -- edge of the box comes from the mirrored centre
    local x = mx(d.x + d.w / 2)
    local y = sy(d.y - d.h / 2)
    screen.rect(x, y, sx(d.w), sy(d.h), 0, 255, 0)
    screen.text(5, 5, "t=" .. d.target .. " s=" .. d.score, 2, 0, 255, 0)
    screen.text(5, screen.height() - 25, vision.count() .. " in frame", 2)
  elseif not vision.ok() then
    screen.text(10, 50, "camera offline", 2, 255, 80, 80)
  else
    screen.text(10, 50, "nothing in frame", 2, 120, 120, 120)
  end
  screen.flip()
end
