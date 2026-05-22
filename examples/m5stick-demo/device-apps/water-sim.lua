-- Water Sim: lo-fi sloshing tank with fish + bubbles, driven by IMU + buttons
local N = 32
local W, H
-- smoothed gravity (stable orientation)
local sx, sy, sz = 0, 0, 1
local gvx, gvy, gvz = 0, 0, 0
local KG, DG = 9.0, 3.2
-- wave height-field (gravity frame, reflecting ends)
local h, v, vd = {}, {}, {}
local TENSION, RDAMP, REST, VISC = 0.35, 0.88, 0.12, 0.04
local FIELD_GAIN, MID = 200.0, (N + 1) / 2
local pgx, pgy, has_prev = 0, 0, false
-- per-tick geometry, shared by fish/bubbles/render
local gx, gy, thr, dmax, umin, uspan = 0, 0, 0, 1, 0, 1
local pending_splash = 0
local BAND = 30  -- depth of the lighter shallow-water band, px
-- 3 lazy fish that dart now and then
local fish = {
  { x = 0, y = 0, vx =  20, vy =   8, boost = 0, cool = 25 },
  { x = 0, y = 0, vx = -18, vy = -10, boost = 0, cool = 55 },
  { x = 0, y = 0, vx =   6, vy =  22, boost = 0, cool = 85 },
}
local CRUISE, DART_MUL = 22, 2.6
-- 5 rising bubbles
local bub = { {x=0,y=0}, {x=0,y=0}, {x=0,y=0}, {x=0,y=0}, {x=0,y=0} }
-- shells on the sand, clustered near the display centre (so they sit near
-- the waterline whatever the orientation)
local NSHELL = 20
local shell = {}
for i = 1, NSHELL do shell[i] = { x = 0, y = 0 } end

local function splash(center)
  local w = 3
  for j = center - w, center + w do
    if j >= 1 and j <= N then
      h[j] = h[j] - 13 * (1 + cos(3.14159265 * (j - center) / w)) / 2
    end
  end
end

local function lerp_h(idx)
  if idx < 1 then idx = 1 elseif idx > N then idx = N end
  local i0 = floor(idx)
  local i1 = i0 + 1
  if i1 > N then i1 = N end
  return h[i0] + (h[i1] - h[i0]) * (idx - i0)
end

local function setspeed(ff, target)
  local sp = sqrt(ff.vx * ff.vx + ff.vy * ff.vy)
  if sp < 0.01 then sp = 0.01 end
  ff.vx = ff.vx / sp * target
  ff.vy = ff.vy / sp * target
end

local function ftri(x0, y0, x1, y1, x2, y2, r, g, b)
  screen.fill_triangle(floor(x0), floor(y0), floor(x1), floor(y1),
    floor(x2), floor(y2), r, g, b)
end

local function update_fish(dt, t, flat)
  for i = 1, 3 do
    local ff = fish[i]
    -- lazy noise wander: rotate heading
    local da = 0.2 * noise2d(i * 5, t * 0.7)
    local c, s = cos(da), sin(da)
    local hx = ff.vx * c - ff.vy * s
    local hy = ff.vx * s + ff.vy * c
    local sp = sqrt(hx * hx + hy * hy)
    if sp < 0.01 then sp = 0.01 end
    hx, hy = hx / sp, hy / sp
    -- gentle steering away from boundaries (curve, never bounce)
    if not flat and ff.x * gx + ff.y * gy < thr + 30 then
      hx, hy = hx + gx * 0.5, hy + gy * 0.5
    end
    if ff.x < 26 then hx = hx + 0.5 end
    if ff.x > W - 26 then hx = hx - 0.5 end
    if ff.y < 26 then hy = hy + 0.5 end
    if ff.y > H - 26 then hy = hy - 0.5 end
    -- occasional spontaneous dart
    ff.cool = ff.cool - 1
    if ff.cool <= 0 then
      ff.boost = 1
      ff.cool = 35 + floor(55 * abs(noise2d(i * 9, t)))
    end
    ff.boost = ff.boost * 0.80
    ff.vx, ff.vy = hx, hy
    setspeed(ff, CRUISE * (1 + ff.boost * DART_MUL))
    ff.x = ff.x + ff.vx * dt
    ff.y = ff.y + ff.vy * dt
    if ff.x < 13 then ff.x = 13 elseif ff.x > W - 13 then ff.x = W - 13 end
    if ff.y < 13 then ff.y = 13 elseif ff.y > H - 13 then ff.y = H - 13 end
  end
end

local function scatter_fish()
  -- bolt away from the splash point (where the ripple appears)
  local u = umin + (pending_splash - 1) / (N - 1) * uspan
  local spx = gx * thr - gy * u
  local spy = gy * thr + gx * u
  for i = 1, 3 do
    local ff = fish[i]
    local dx, dy = ff.x - spx, ff.y - spy
    local dd = sqrt(dx * dx + dy * dy)
    if dd < 1 then dx, dy, dd = 1, 0, 1 end
    ff.vx, ff.vy = dx / dd, dy / dd
    ff.boost = 1
  end
end

local function update_bub(t, flat)
  if flat then return end
  for i = 1, 5 do
    local b = bub[i]
    local wob = noise2d(i * 3, t * 1.5) * 0.7
    b.x = b.x - gx * 2.0 - gy * wob
    b.y = b.y - gy * 2.0 + gx * wob
    if b.x * gx + b.y * gy < thr + 3 then
      local u = umin + uspan * (0.15 + 0.7 * abs(noise2d(i * 7, t)))
      local d = thr + (dmax - thr) * 0.82
      b.x = gx * d - gy * u
      b.y = gy * d + gx * u
    end
  end
end

local function draw_bub()
  for i = 1, 5 do
    screen.rect(floor(bub[i].x) - 1, floor(bub[i].y) - 1, 3, 3, 200, 222, 226)
  end
end

local function draw_shells()
  for i = 1, NSHELL do
    local cx, cy = shell[i].x, shell[i].y
    local br, bg, bb, ar, ag, ab
    if i % 3 == 0 then
      br, bg, bb, ar, ag, ab = 192, 152, 152, 230, 192, 190  -- dusty pink
    else
      br, bg, bb, ar, ag, ab = 198, 184, 158, 240, 230, 212  -- cream
    end
    -- darker base + lighter inset, both fills, so the rim always lines up
    screen.fill_triangle(cx, cy + 3, cx - 5, cy - 2, cx + 5, cy - 2, br, bg, bb)
    screen.fill_triangle(cx, cy + 1, cx - 3, cy - 1, cx + 3, cy - 1, ar, ag, ab)
  end
end

local function draw_fish()
  for i = 1, 3 do
    local ff = fish[i]
    local sp = sqrt(ff.vx * ff.vx + ff.vy * ff.vy)
    if sp < 0.01 then sp = 0.01 end
    local fwx, fwy = ff.vx / sp, ff.vy / sp
    local rx, ry = -fwy, fwx
    local x, y = ff.x, ff.y
    local bx, by = x - fwx * 2, y - fwy * 2
    local tx, ty = x - fwx * 12, y - fwy * 12
    ftri(bx, by, tx + rx * 6, ty + ry * 6, tx - rx * 6, ty - ry * 6,
      198, 104, 58)
    ftri(x + fwx * 12, y + fwy * 12, bx + rx * 6, by + ry * 6,
      bx - rx * 6, by - ry * 6, 228, 138, 74)
    screen.fill_rect(floor(x + fwx * 5 + rx * 2 - 1),
      floor(y + fwy * 5 + ry * 2 - 1), 2, 2, 46, 42, 54)
  end
end

function init(ctx)
  W, H = screen.width(), screen.height()
  sx, sy, sz = imu.accel()
  for i = 1, N do h[i] = 0; v[i] = 0; vd[i] = 0 end
  local px = { 0.30, 0.62, 0.48 }
  local py = { 0.45, 0.62, 0.30 }
  for i = 1, 3 do
    fish[i].x, fish[i].y = W * px[i], H * py[i]
    setspeed(fish[i], CRUISE)
  end
  local bx = { 0.25, 0.45, 0.60, 0.78, 0.52 }
  local by = { 0.72, 0.88, 0.62, 0.80, 0.95 }
  for i = 1, 5 do bub[i].x, bub[i].y = W * bx[i], H * by[i] end
  for i = 1, NSHELL do
    -- golden-ratio scatter: even spread across the whole display, no clumps
    shell[i].x = floor(12 + fract(i * 0.6180) * (W - 24))
    shell[i].y = floor(12 + fract(i * 0.4142) * (H - 24))
  end
end

function on_event(ctx, e)
  if e.name == "button" then
    local c = e.index == 0 and floor(N * 0.3) or floor(N * 0.7)
    splash(c)
    pending_splash = c
  end
end

function on_tick(ctx, dt_ms)
  local dt = dt_ms / 1000
  if dt > 0.1 then dt = 0.1 end
  local t = ctx.time_ms * 0.001

  -- smoothed gravity
  local ax, ay, az = imu.accel()
  gvx = gvx + (ax - sx) * KG * dt - gvx * DG * dt
  gvy = gvy + (ay - sy) * KG * dt - gvy * DG * dt
  gvz = gvz + (az - sz) * KG * dt - gvz * DG * dt
  sx, sy, sz = sx + gvx * dt, sy + gvy * dt, sz + gvz * dt

  local f = (sz + 1) / 2
  if f < 0 then f = 0 elseif f > 1 then f = 1 end

  local gmag = sqrt(sx * sx + sy * sy)
  local flat = gmag < 0.08
  if not flat then
    gx, gy = sx / gmag, sy / gmag
    -- rotating the device pumps travelling waves into the height-field
    if has_prev then
      local dtheta = pgx * gy - pgy * gx
      for i = 1, N do
        local s = (i - MID) / MID
        v[i] = v[i] + FIELD_GAIN * dtheta * s * s * s
      end
    end
    pgx, pgy, has_prev = gx, gy, true
  else
    has_prev = false
  end

  -- height-field: Laplacian propagation
  for i = 1, N do
    local l = h[i - 1] or h[1]
    local r = h[i + 1] or h[N]
    v[i] = (v[i] + TENSION * (l + r - 2 * h[i]) - REST * h[i]) * RDAMP
  end
  -- viscosity: diffuse velocity so small ripples disperse (water, not jelly)
  for i = 1, N do
    local vl = v[i - 1] or v[1]
    local vr = v[i + 1] or v[N]
    vd[i] = v[i] + VISC * (vl + vr - 2 * v[i])
  end
  for i = 1, N do
    v[i] = vd[i]
    h[i] = h[i] + v[i]
    if h[i] > 60 then h[i] = 60 elseif h[i] < -60 then h[i] = -60 end
  end

  -- geometry: water threshold + along-surface mapping
  if not flat then
    local dmin = min(min(0, W * gx), min(H * gy, W * gx + H * gy))
    dmax = max(max(0, W * gx), max(H * gy, W * gx + H * gy))
    thr = dmin + (1 - f) * (dmax - dmin)
    umin = min(min(0, -gy * W), min(gx * H, -gy * W + gx * H))
    local umax = max(max(0, -gy * W), max(gx * H, -gy * W + gx * H))
    uspan = umax - umin
    if uspan < 1 then uspan = 1 end
  end

  if pending_splash > 0 then
    if not flat then scatter_fish() end
    pending_splash = 0
  end
  update_fish(dt, t, flat)
  update_bub(t, flat)

  screen.clear(220, 192, 150)  -- warm sand

  if flat then
    if f > 0.5 then screen.clear(106, 166, 184) else draw_shells() end
    draw_bub()
    draw_fish()
    screen.flip()
    return
  end

  draw_shells()  -- on the sand; the water strips below cover submerged ones

  local step = 2
  if abs(gy) >= abs(gx) then
    for x = 0, W - 1, step do
      local y0 = (thr - x * gx) / gy
      local idx = 1 + (-gy * x + gx * y0 - umin) / uspan * (N - 1)
      local ys = (thr - lerp_h(idx) - x * gx) / gy
      if gy > 0 then
        local yt = floor(ys)
        if yt < 0 then yt = 0 end
        if yt < H then
          screen.fill_rect(x, yt, step, H - yt, 60, 116, 144)
          screen.fill_rect(x, yt, step, BAND, 106, 166, 184)
          screen.fill_rect(x, yt, step, 3, 206, 226, 224)
        end
      else
        local yb = ceil(ys)
        if yb > H then yb = H end
        if yb > 0 then
          screen.fill_rect(x, 0, step, yb, 60, 116, 144)
          screen.fill_rect(x, yb - BAND, step, BAND, 106, 166, 184)
          screen.fill_rect(x, yb - 3, step, 3, 206, 226, 224)
        end
      end
    end
  else
    for y = 0, H - 1, step do
      local x0 = (thr - y * gy) / gx
      local idx = 1 + (-gy * x0 + gx * y - umin) / uspan * (N - 1)
      local xs = (thr - lerp_h(idx) - y * gy) / gx
      if gx > 0 then
        local xt = floor(xs)
        if xt < 0 then xt = 0 end
        if xt < W then
          screen.fill_rect(xt, y, W - xt, step, 60, 116, 144)
          screen.fill_rect(xt, y, BAND, step, 106, 166, 184)
          screen.fill_rect(xt, y, 3, step, 206, 226, 224)
        end
      else
        local xb = ceil(xs)
        if xb > W then xb = W end
        if xb > 0 then
          screen.fill_rect(0, y, xb, step, 60, 116, 144)
          screen.fill_rect(xb - BAND, y, BAND, step, 106, 166, 184)
          screen.fill_rect(xb - 3, y, 3, step, 206, 226, 224)
        end
      end
    end
  end

  draw_bub()
  draw_fish()
  screen.flip()
end
