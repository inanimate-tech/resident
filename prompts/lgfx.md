# lgfx — immediate-mode drawing

This device draws with `lgfx`, Lua bindings over LovyanGFX — the drawing API
you already know. Displays are registered by name; bind one and colon-call
the handle.

```lua
local g = lgfx.bind("main")       -- errors on unknown names
g:fillScreen(0x000000)
g:fillCircle(80, 60, 20, 0xFF5533)
g:setTextColor(0xFFFFFF); g:setTextSize(2)
g:setTextDatum(lgfx.MC_DATUM)
g:drawString("hello", g:width() // 2, g:height() // 2)
g:flip()                          -- nothing reaches the glass until this
```

## API (colon-call; colors are 24-bit 0xRRGGBB)

- `g:fillScreen(c)` · `g:drawPixel(x,y,c)` · `g:drawLine(x0,y0,x1,y1,c)`
- `g:drawRect/fillRect(x,y,w,h,c)` · `g:drawRoundRect/fillRoundRect(x,y,w,h,r,c)`
- `g:drawCircle/fillCircle(x,y,r,c)` · `g:drawTriangle/fillTriangle(x0,y0,x1,y1,x2,y2,c)`
- Text: `g:setTextColor(fg[,bg])` · `g:setTextSize(n)` · `g:drawString(s,x,y)`
  placed by `g:setTextDatum(d)` (datum constants on the module:
  `lgfx.TL_DATUM`, `TC`, `TR`, `ML`, `MC`, `MR`, `BL`, `BC`, `BR`,
  `L_BASELINE`, `C_BASELINE`, `R_BASELINE`) · or `g:setCursor(x,y)` + `g:print(s)`
- Pixels: `g:pushImage(x,y,w,h,data[,palette[,transparent]])` · clip with
  `g:setClipRect(x,y,w,h)` / `g:clearClipRect()` (see "Pixels from a string")
- `g:width()` · `g:height()` · `g:flip()`

## Binding is claiming

`lgfx.bind(name)` declares the library this app draws that surface with AND
claims the surface. One panel, one library: if the same app also calls
`lvgl.bind(name)` on that surface, LVGL takes it over and every later
`g:flip()` is dropped silently (the drawing calls still run — they just never
reach the glass). Pick one library per surface and stay with it. Ownership
resets when a new app loads, so the next app's first bind is clean.

## Pixels from a string

`g:pushImage(x, y, w, h, data)` draws a block of pixels in one call. `data` is
a string of exactly `w * h * 2` bytes: RGB565, high byte first, row by row.
Any other length raises. Use it when the app keeps its own picture and only
part of it changed: one call instead of a `fillRect` per run of one colour.

```lua
local function px(c)              -- 0xRRGGBB -> 2 bytes of RGB565
  local v = ((c >> 19) << 11) | (((c >> 10) & 0x3F) << 5) | ((c >> 3) & 0x1F)
  return string.char(v >> 8, v & 0xFF)
end
g:pushImage(10, 10, 4, 1, px(0xFF0000):rep(2) .. px(0x0000FF):rep(2))
```

With a palette, `data` is one byte a pixel: `g:pushImage(x, y, w, h, data,
palette, transparent)`, where `palette` is a string of 1 to 256 RGB565 colours
(2 bytes each) and `data` is `w * h` indices into it. An index equal to
`transparent` (optional) or past the palette's end is not drawn, so a sprite
sits over what is already there. Build the strings once, in `init`.

`g:setClipRect(x, y, w, h)` limits every later draw to that rectangle until
`g:clearClipRect()`: push a backdrop and the sprites over it whole, and only
the clipped cell changes. A new app starts with no clip.

## Rules

- Default font only. Size `n` renders ~6·n px per character, ~8·n px tall.
  No wrapping — break long text into lines yourself.
- No image files, no extra sprites, no font selection. This is the whole surface.
- Draw back to front: fills first, text last, then ONE `g:flip()`.
- Never flip an unchanged frame. Track what the frame depends on and return
  early when nothing changed:

```lua
local prev = {}
local function draw()
  local frame = table.concat({ tostring(state), tostring(value) }, "|")
  if prev.frame == frame then return end
  prev.frame = frame
  g:fillScreen(0x000000)
  -- ...draw...
  g:flip()
end
```
