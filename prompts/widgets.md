# Shader and Lgfx — widgets your own function paints

Two lvgl widgets whose pixels come from a Lua function in the app. Create
them on a parent object — `parent:Shader{...}`, `parent:Lgfx{...}`, the
screen (`h.screen()`) or any container — not on the display handle, whose
constructors are lvgl.md's list. Every base-object property (x, y, w, h,
align, radius, opa, ...) works too.

## Shader — a field

```lua
local h = lvgl.bind("main")
local scr = h.screen()
local sky = scr:Shader{ palette = "ember", fn = function(x, y, t, u, v)
  return shader.fbm(x * 3, y * 3 + t * 0.4)
end }
```

- `fn(x, y, t, u, v)` is called once per CELL, a block of pixels the widget
  paints one colour. x, y are centred, the SHORTER side spans -1..1, y up;
  t is seconds; u, v run 0..1 across the widget.
- Return ONE number 0..1, looked up in `palette`, or THREE, r, g, b 0..1.
- `palette`: `ember`, `ocean`, `aurora`, `sunset`, `rainbow`, `mono` (the
  default), or a list of 2..8 colours, dark to light.
- `speed` (1; 0 freezes t), `scale` (1; 2 zooms in), `cell` (block edge in
  px, 1..16; leave it unset: the widget aims at about 1,600 cells and
  coarsens when a frame's Lua runs past 25 ms), `fps` (15, at most 30; 0 =
  draw only when a key is set).
- It fills its parent unless given x, y, w, h. `radius` rounds it
  (`lvgl.RADIUS_CIRCLE` for a disc).
- The global `shader`, Shadertoy's kit in C: `fract`, `mix`, `clamp`,
  `step`, `smoothstep`, `mod`, `length(x, y)`, `hash(x, y)`,
  `noise(x, y)`, `fbm(x, y[, octaves])` (all 0..1), `hsv(h, s, v)` -> r, g,
  b, signed distances `circle(x, y, r)`, `box(x, y, hw, hh)`,
  `segment(x, y, ax, ay, bx, by)`, `smin(a, b, k)`, and `rot(x, y, angle)`
  -> x, y. `math.*` for the rest.
- `fn` runs thousands of times a frame: arithmetic only, no tables, no
  strings; hoist constants out of it.

## Lgfx — geometry you draw every frame

```lua
local scope = scr:Lgfx{ w = 128, h = 46, fps = 20, fn = function(g, t)
  g:fillScreen(0x000000)
  g:drawLine(0, 23, 127, 23 + 20 * math.sin(t), 0xffffff)
end }
```

- `fn(g, t)` is called once a frame with a pen `g`, in the widget's own
  coordinates, and t in seconds.
- Pen calls: `fillScreen(c)`, `drawPixel(x, y, c)`,
  `drawLine(x0, y0, x1, y1, c)`, `drawFastHLine(x, y, w, c)`,
  `drawFastVLine(x, y, h, c)`, `drawRect`/`fillRect(x, y, w, h, c)`,
  `drawRoundRect`/`fillRoundRect(x, y, w, h, r, c)`,
  `drawCircle`/`fillCircle(x, y, r, c)`,
  `drawEllipse`/`fillEllipse(x, y, rx, ry, c)`,
  `drawTriangle`/`fillTriangle(x0, y0, x1, y1, x2, y2, c)`,
  `drawArc`/`fillArc(x, y, r0, r1, a0, a1, c)` (degrees clockwise from 3
  o'clock), `setTextColor(fg[, bg])`, `setTextSize(n)`,
  `setTextDatum(g.MC_DATUM)` (`TL_ TC_ TR_ ML_ MC_ MR_ BL_ BC_ BR_DATUM`,
  `L_ C_ R_BASELINE`), `setCursor(x, y)`, `print(s)`,
  `drawString(s, x, y)`, `width()`, `height()`, `color888(r, g, b)`.
- Colours are `0xRRGGBB` or `"#RRGGBB"`. Coordinates may be fractional;
  they round. Nothing is anti-aliased.
- The picture is kept between frames: clear it with `g:fillScreen`, or
  leave trails.
- Opaque unless `transparent = colour`: pixels of that colour show what is
  behind.
- `fps` (15, at most 30; 0 = draw only when a key is set). `g` draws only
  while `fn` runs.

## Rules

- A new function takes effect at once: `w:set{ fn = ... }`. `fn` may read
  globals your timers and handlers change.
- An error in `fn` is logged and stops that widget until it is given a new
  `fn`.
- A frame is made once per `fps` tick and only while the widget is drawn,
  so a hidden or covered widget costs nothing.
- Never set a key on a widget from inside its own `fn`.
- Words stay Labels; use these for fields and moving geometry.
