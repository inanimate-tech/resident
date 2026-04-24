-- Cat mood: sleepy <-> alert (FEED ME!)
-- Press either button to toggle.

local mode = "sleepy"

local function draw(t_ms)
  screen.clear(20, 20, 40)

  -- Breathing pulse (+/-1 px squash/stretch)
  local pulse = math.floor(math.sin(t_ms / 500) * 1.5)
  local hy, hh = 25 + pulse, 85 - pulse

  -- Ears (orange outer, pink inner)
  screen.fill_triangle(30, hy, 55, hy, 35, hy - 20, 230, 140, 60)
  screen.fill_triangle(105, hy, 130, hy, 125, hy - 20, 230, 140, 60)
  screen.fill_triangle(35, hy, 50, hy, 40, hy - 12, 255, 180, 180)
  screen.fill_triangle(110, hy, 125, hy, 122, hy - 12, 255, 180, 180)

  -- Head
  screen.fill_rect(30, hy, 100, hh, 230, 140, 60)

  -- Whiskers
  screen.line(5, hy + 45, 40, hy + 48, 240, 240, 240)
  screen.line(5, hy + 52, 40, hy + 52, 240, 240, 240)
  screen.line(5, hy + 59, 40, hy + 56, 240, 240, 240)
  screen.line(120, hy + 48, 155, hy + 45, 240, 240, 240)
  screen.line(120, hy + 52, 155, hy + 52, 240, 240, 240)
  screen.line(120, hy + 56, 155, hy + 59, 240, 240, 240)

  -- Nose
  screen.fill_triangle(75, hy + 42, 85, hy + 42, 80, hy + 50, 220, 100, 120)

  if mode == "sleepy" then
    -- Closed eyes (shallow V -> ^ shape)
    screen.line(42, hy + 28, 52, hy + 25, 0, 0, 0)
    screen.line(52, hy + 25, 62, hy + 28, 0, 0, 0)
    screen.line(92, hy + 28, 102, hy + 25, 0, 0, 0)
    screen.line(102, hy + 25, 112, hy + 28, 0, 0, 0)
    -- Tiny smile
    screen.line(75, hy + 55, 80, hy + 58, 0, 0, 0)
    screen.line(80, hy + 58, 85, hy + 55, 0, 0, 0)
    -- Zzz
    screen.text(155, 20, "Z", 3, 200, 200, 255)
    screen.text(185, 45, "z", 2, 200, 200, 255)
    screen.text(208, 65, "z", 2, 200, 200, 255)
  else
    -- Wide dilated eyes
    screen.fill_rect(42, hy + 20, 22, 22, 255, 255, 255)
    screen.fill_rect(92, hy + 20, 22, 22, 255, 255, 255)
    screen.fill_rect(49, hy + 26, 10, 12, 0, 0, 0)
    screen.fill_rect(99, hy + 26, 10, 12, 0, 0, 0)
    screen.fill_rect(51, hy + 27, 2, 2, 255, 255, 255)
    screen.fill_rect(101, hy + 27, 2, 2, 255, 255, 255)
    -- Open shouting mouth
    screen.fill_triangle(72, hy + 55, 88, hy + 55, 80, hy + 70, 80, 20, 20)
    -- FEED ME!
    screen.text(148, 30, "FEED", 3, 255, 80, 80)
    screen.text(160, 70, "ME!", 3, 255, 80, 80)
  end

  screen.flip()
end

function init(ctx)
  mode = kv.get("mode") or "sleepy"
  draw(ctx.time_ms)
end

function on_tick(ctx, dt_ms)
  draw(ctx.time_ms)
end

function on_event(ctx, e)
  if e.name == "button" then
    if mode == "sleepy" then
      mode = "alert"
      buzzer.beep(880, 120)
    else
      mode = "sleepy"
    end
    kv.set("mode", mode)
    draw(ctx.time_ms)
  end
end
