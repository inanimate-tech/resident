local mode = 0
local hue = 0
local colors = {{255,80,80},{80,255,120},{80,160,255}}
local mode_names = {"RECT", "TRI", "LINES"}

local function draw()
  local c = colors[hue + 1]
  screen.clear()
  screen.text(8, 6, "BTN0:MODE  BTN1:HUE", 1, 180, 180, 180)
  screen.text(8, 22, mode_names[mode + 1] .. " #" .. (hue + 1), 2, c[1], c[2], c[3])
  if mode == 0 then
    screen.fill_rect(60, 60, 120, 60, c[1], c[2], c[3])
    screen.rect(58, 58, 124, 64, 255, 255, 255)
  elseif mode == 1 then
    screen.fill_triangle(120, 55, 60, 125, 180, 125, c[1], c[2], c[3])
  else
    for i = 0, 10 do
      screen.line(20 + i * 20, 60, 220 - i * 20, 125, c[1], c[2], c[3])
    end
  end
  screen.flip()
end

function init(ctx)
  draw()
end

function on_event(ctx, e)
  if e.name == "button" then
    if e.index == 0 then
      mode = (mode + 1) % 3
      buzzer.beep(440, 80)
    else
      hue = (hue + 1) % 3
      buzzer.beep(880, 80)
    end
    draw()
  end
end
