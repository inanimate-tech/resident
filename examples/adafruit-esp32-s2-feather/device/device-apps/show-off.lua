local t = 0
local scroll_x = 135
local scroller = "*** RESIDENT 4 LIFE *** GREETINGS TO ALL DEMOSCENERS *** MADE IN LONDON *** PUSHED FROM CLAUDE CODE *** "

function init(ctx)
  led.set_brightness(30)
end

function on_tick(ctx, dt_ms)
  t = ctx.time_ms / 1000
  screen.clear()

  for y = 0, 236, 4 do
    local p = t * 1.5 + y * 0.04
    local r = math.floor(127 + 127 * math.sin(p))
    local g = math.floor(127 + 127 * math.sin(p + 2.094))
    local b = math.floor(127 + 127 * math.sin(p + 4.188))
    screen.fill_rect(0, y, 135, 4, r, g, b)
  end

  local title = "RESIDENT"
  local base_y = 28
  local char_w = 12
  for i = 1, #title do
    local ch = title:sub(i, i)
    local x = 20 + (i - 1) * char_w
    local yoff = math.floor(10 * math.sin(t * 4 + i * 0.6))
    screen.text(x + 1, base_y + yoff + 1, ch, 2, 0, 0, 0)
    screen.text(x, base_y + yoff, ch, 2, 255, 255, 255)
  end

  scroll_x = scroll_x - 2
  if scroll_x < -(#scroller * 6) then scroll_x = 135 end
  screen.fill_rect(0, 218, 135, 14, 0, 0, 0)
  screen.text(scroll_x, 222, scroller, 1, 0, 255, 255)

  local pr = math.floor(127 + 127 * math.sin(t * 1.5))
  local pg = math.floor(127 + 127 * math.sin(t * 1.5 + 2.094))
  local pb = math.floor(127 + 127 * math.sin(t * 1.5 + 4.188))
  led.set(pr, pg, pb)

  screen.flip()
end
