-- Cat + Claude: goes into FEED ME mode when a Claude Code permission event
-- arrives, and overlays the tool/summary on the display. Press either button
-- to dismiss the overlay and go back to sleepy.

local mode = "sleepy"
local permission_tool = nil
local permission_summary = nil

local function tune_up()
  buzzer.beep(523, 80)
  buzzer.beep(659, 80)
  buzzer.beep(784, 120)
end

local function tune_down()
  buzzer.beep(784, 80)
  buzzer.beep(659, 80)
  buzzer.beep(523, 120)
end

local function draw(t_ms)
  -- Lo-fi meadow scene: sky, sun, cloud, hills, sill, window frame.
  -- Drawn before the cat so the cat sits in front.
  screen.clear(130, 180, 220)                                -- sky
  screen.fill_rect(200, 15, 16, 16, 240, 210, 90)            -- sun
  screen.fill_rect(20, 20, 30, 10, 230, 230, 230)            -- cloud
  screen.fill_rect(35, 14, 25, 10, 230, 230, 230)
  screen.fill_rect(15, 25, 30, 10, 230, 230, 230)
  screen.fill_triangle(0, 120, 140, 120, 70, 95, 100, 150, 90)  -- back hill
  screen.fill_triangle(100, 120, 240, 120, 180, 100, 80, 130, 70) -- front hill
  screen.fill_rect(0, 120, 240, 15, 140, 100, 60)            -- sill
  screen.fill_rect(0, 0, 240, 4, 90, 55, 30)                 -- window frame top
  screen.fill_rect(0, 131, 240, 4, 90, 55, 30)               --              bottom
  screen.fill_rect(0, 0, 4, 135, 90, 55, 30)                 --              left
  screen.fill_rect(236, 0, 4, 135, 90, 55, 30)               --              right

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

  -- Claude permission overlay (stacks on top of everything)
  if permission_tool then
    screen.fill_rect(0, 100, 240, 35, 0, 0, 0)
    screen.text(2, 104, permission_tool, 2, 255, 255, 255)
    screen.text(2, 120, permission_summary, 2, 255, 255, 255)
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
      tune_up()
    else
      mode = "sleepy"
      permission_tool = nil
      permission_summary = nil
      tune_down()
    end
    kv.set("mode", mode)
    draw(ctx.time_ms)
  elseif e.name == "permission" then
    mode = "alert"
    permission_tool = e.data.tool or ""
    permission_summary = e.data.summary or ""
    kv.set("mode", mode)
    tune_up()
    draw(ctx.time_ms)
  end
end
