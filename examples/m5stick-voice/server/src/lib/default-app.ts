/**
 * Default Lua app shown in the sim when no create_app has succeeded yet.
 * Useful for a smoke test: if the bouncing ball doesn't show, fengari-web
 * isn't loading in the browser. Lifted from m5stick-demo's examples.
 */
export const DEFAULT_APP = `
local x, y = 120, 67
local dx, dy = 2.0, 1.5
local r = 8

function init(ctx)
  screen.clear()
  screen.text(8, 8, "HELLO", 2, 255, 255, 255)
  screen.text(8, 30, "DEFAULT APP", 1, 180, 180, 180)
  screen.flip()
end

function on_tick(ctx, dt_ms)
  x = x + dx
  y = y + dy
  if x <= r or x >= 240 - r then dx = -dx end
  if y <= r or y >= 135 - r then dy = -dy end

  screen.clear()
  screen.fill_rect(math.floor(x - r), math.floor(y - r), r * 2, r * 2, 0, 255, 80)
  screen.text(8, 8, "DEFAULT", 1, 120, 120, 120)
  screen.flip()
end
`
