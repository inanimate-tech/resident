-- Swiss railway clock (Mondaine-style): white face, bar markers, red lollipop
local PI = 3.14159265
local CX, CY, R

local function ftri(x0, y0, x1, y1, x2, y2, r, g, b)
  screen.fill_triangle(floor(x0), floor(y0), floor(x1), floor(y1),
    floor(x2), floor(y2), r, g, b)
end

-- thick bar from (x0,y0) to (x1,y1), width w
local function bar(x0, y0, x1, y1, w, r, g, b)
  local dx, dy = x1 - x0, y1 - y0
  local len = sqrt(dx * dx + dy * dy)
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
  R = min(W, H) / 2 - 8
end

function on_tick(ctx, dt_ms)
  local hr = ctx.localtime_h
  local mn = ctx.localtime_m
  local sc = time.second()

  local th = ((hr % 12) + mn / 60) * PI / 6
  local tm = (mn + sc / 60) * PI / 30
  local ts = sc * PI / 30

  screen.clear(250, 250, 248)  -- warm white face

  -- 12 hour bar markers
  for k = 0, 11 do
    local a = k * PI / 6
    local s, c = sin(a), -cos(a)
    bar(CX + 0.86 * R * s, CY + 0.86 * R * c,
        CX + 0.96 * R * s, CY + 0.96 * R * c, 4, 28, 28, 32)
  end

  -- hour hand
  local hs, hc = sin(th), -cos(th)
  bar(CX, CY, CX + 0.55 * R * hs, CY + 0.55 * R * hc, 6, 28, 28, 32)

  -- minute hand
  local ms, mc = sin(tm), -cos(tm)
  bar(CX, CY, CX + 0.88 * R * ms, CY + 0.88 * R * mc, 4, 28, 28, 32)

  -- second hand: thin red line + red lollipop disc at the tip
  local ss, sk = sin(ts), -cos(ts)
  local tipx, tipy = CX + 0.90 * R * ss, CY + 0.90 * R * sk
  screen.line(floor(CX), floor(CY), floor(tipx), floor(tipy), 220, 50, 50)
  local d = 3
  ftri(tipx, tipy - d, tipx - d, tipy, tipx + d, tipy, 220, 50, 50)
  ftri(tipx - d, tipy, tipx + d, tipy, tipx, tipy + d, 220, 50, 50)

  -- central pivot
  screen.fill_rect(floor(CX) - 2, floor(CY) - 2, 5, 5, 28, 28, 32)

  screen.flip()
end
