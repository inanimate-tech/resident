-- Swiss railway clock (Mondaine-style): white face, bar markers, red lollipop
local PI = 3.14159265
local CX, CY, R

local function ftri(x0, y0, x1, y1, x2, y2, r, g, b)
  local f = math.floor
  screen.fill_triangle(f(x0), f(y0), f(x1), f(y1), f(x2), f(y2), r, g, b)
end

-- thick bar from (x0,y0) to (x1,y1), width w
local function bar(x0, y0, x1, y1, w, r, g, b)
  local dx, dy = x1 - x0, y1 - y0
  local len = math.sqrt(dx * dx + dy * dy)
  if len < 0.01 then return end
  local nx, ny = dx / len, dy / len
  local px, py = -ny, nx
  local hw = w / 2
  local ax, ay = x0 + px * hw, y0 + py * hw
  local bxx, byy = x0 - px * hw, y0 - py * hw
  local cxx, cyy = x1 - px * hw, y1 - py * hw
  local dxx, dyy = x1 + px * hw, y1 + py * hw
  ftri(ax, ay, bxx, byy, cxx, cyy, r, g, b)
  ftri(ax, ay, cxx, cyy, dxx, dyy, r, g, b)
end

function init(ctx)
  local W, H = screen.width(), screen.height()
  CX, CY = W / 2, H / 2
  R = math.min(W, H) / 2 - 8
end

function on_tick(ctx, dt_ms)
  local t = time.localtime()   -- local once the server's timezone is applied
  local hr, mn, sc = t.tm_hour, t.tm_min, t.tm_sec

  local th = ((hr % 12) + mn / 60) * PI / 6
  local tm = (mn + sc / 60) * PI / 30
  local ts = sc * PI / 30

  screen.clear(250, 250, 248)  -- warm white face

  -- 12 hour bar markers
  for k = 0, 11 do
    local a = k * PI / 6
    local s, c = math.sin(a), -math.cos(a)
    bar(CX + 0.86 * R * s, CY + 0.86 * R * c,
        CX + 0.96 * R * s, CY + 0.96 * R * c, 4, 28, 28, 32)
  end

  -- hour hand
  local hs, hc = math.sin(th), -math.cos(th)
  bar(CX, CY, CX + 0.55 * R * hs, CY + 0.55 * R * hc, 6, 28, 28, 32)

  -- minute hand
  local ms, mc = math.sin(tm), -math.cos(tm)
  bar(CX, CY, CX + 0.88 * R * ms, CY + 0.88 * R * mc, 4, 28, 28, 32)

  -- second hand: thin red line + red lollipop disc at the tip
  local ss, sk = math.sin(ts), -math.cos(ts)
  local tipx, tipy = CX + 0.90 * R * ss, CY + 0.90 * R * sk
  screen.line(math.floor(CX), math.floor(CY), math.floor(tipx), math.floor(tipy), 220, 50, 50)
  local d = 3
  ftri(tipx, tipy - d, tipx - d, tipy, tipx + d, tipy, 220, 50, 50)
  ftri(tipx - d, tipy, tipx + d, tipy, tipx, tipy + d, 220, 50, 50)

  -- central pivot
  screen.fill_rect(math.floor(CX) - 2, math.floor(CY) - 2, 5, 5, 28, 28, 32)

  screen.flip()
end
