# lvgl — retained-mode UI

This device builds UIs with `lvgl`, Lua bindings (luavgl) over LVGL 9.
Retained mode: you construct a widget tree once, then mutate properties;
LVGL redraws only what changed. Displays are registered by name; bind one
and create widgets on its handle.

```lua
local h = lvgl.bind("main")            -- errors on unknown names
h:set_theme { screen = { bg_color = "#101018" } }
local label = h.Label {
  text = "hello",
  text_color = "#ececf2",
  text_font = lvgl.Font("montserrat", 24),
  align = lvgl.ALIGN.CENTER,
}
function on_tick(ctx, dt_ms)
  label:set { text = os_free_text or "hello" }   -- mutate, don't rebuild
end
```

## Binding and the handle

- `lvgl.bind(name)` returns a display-scoped handle; binding the same name
  twice returns the same handle. Boards with several panels register several
  names — each handle's widgets land only on its own display.
- Widget constructors on the handle (dot and colon calls are equivalent):
  `Object`, `Label`, `Button`, `Image`, `Line`, `Arc`, `Led`, `Checkbox`,
  `Dropdown`, `Roller`, `Textarea`, `Scale`, `List`, `Keyboard`, `Calendar`.
  With no explicit parent they land on the display's active screen. Explicit
  parent: `h.Label(box, { text = "child" })` or `box:Label { text = "child" }`.
- Other handle members: `h.screen()` (active screen as an object — style it
  directly), `h.clean()` (wipe the whole screen; existing Lua handles become
  invalid), `h.HOR_RES()` / `h.VER_RES()` (this display's resolution),
  `h:set_theme{...}`, `h.disp`, `h.mirror()`, `h.set_default()`.
- Everything else on `lvgl` falls through to the full module: `lvgl.Font`,
  `lvgl.Anim`, `lvgl.Style`, `lvgl.Timer`, and all constants below. These
  resolve only after the first `lvgl.bind(...)` call — always bind first.

## Widgets are children with property tables

The constructor's table is one `obj:set{}` call. Nested child widgets are
built on the returned object:

```lua
local card = h.Object(nil, { w = 100, h = 60, x = 8, y = 8,
                             bg_color = "#202030", radius = 8, border_width = 0 })
card:Label { text = "cpu", text_color = "#8888aa", align = lvgl.ALIGN.TOP_LEFT }
local value = card:Label { text = "0%", align = lvgl.ALIGN.BOTTOM_RIGHT }
```

## Object methods (colon-call)

- `obj:set { k = v, ... }` — set any properties/styles after creation
- `obj:center()` — shorthand for align CENTER
- `obj:align_to { base = other, type = lvgl.ALIGN.OUT_BOTTOM_MID, x_ofs = 0, y_ofs = 4 }`
- `obj:delete()` — remove the widget (and its children)
- `obj:clean()` — remove only the children
- `obj:add_flag(f)` / `obj:clear_flag(f)` — e.g. `lvgl.FLAG.HIDDEN`,
  `lvgl.FLAG.SCROLLABLE`, `lvgl.FLAG.CLICKABLE`
- `obj:add_state(s)` / `obj:clear_state(s)` — e.g. `lvgl.STATE.CHECKED`
- `obj:onClicked(function(target, code) end)` ·
  `obj:onPressed(fn)` · `obj:onShortClicked(fn)` ·
  `obj:onevent(lvgl.EVENT.VALUE_CHANGED, fn)` (pass `nil` fn to remove)
- `obj:Anim { ... }` — see Animation below

## Property vocabulary

Geometry and placement: `w`, `h`, `x`, `y`, `align` (an `lvgl.ALIGN.*`
value). Sizes/coords accept `lvgl.PCT(50)`, `lvgl.SIZE_CONTENT`.

Any LVGL style property applies directly in a property table. The useful
set: `bg_color`, `bg_opa`, `radius` (`lvgl.RADIUS_CIRCLE` for round),
`border_width`, `border_color`, `border_opa`, `outline_width`,
`outline_color`, `pad_all`, `pad_top`/`pad_bottom`/`pad_left`/`pad_right`,
`pad_row`, `pad_column`, `opa`, `text_color`, `text_font`, `text_align`
(`lvgl.TEXT_ALIGN.CENTER`), `text_letter_space`, `text_line_space`,
`translate_x`, `translate_y`, `transform_rotation`, `line_width`,
`line_color`, `line_rounded`, `arc_width`, `arc_color`, `arc_rounded`,
`shadow_width`, `shadow_color`, `shadow_opa`.

Colors are `"#RGB"` / `"#RRGGBB"` strings or `0xRRGGBB` integers. Opacity
is 0–255 or `lvgl.OPA(pct)`.

Widget-specific properties:

- `Label`: `text`
- `Arc`: `value`, `angles = {start_deg, end_deg}`, `bg_angles = {s, e}`,
  `rotation`, `mode`, `change_rate` (style the indicator via
  `arc_color`/`arc_width`)
- `Line`: `points = {{x, y}, {x, y}, ...}`, `y_invert`
- `Led`: `color`, `brightness` (0–255); methods `led:on()`, `led:off()`,
  `led:toggle()`
- `Checkbox`: `text`
- `Roller`: `options = "a\nb\nc"`, `selected`; `roller:get_selected_str()`
- `Dropdown`: methods `open()`, `close()`, `add_option(s, pos)`,
  `option_index(s)`

## Fonts

`lvgl.Font(name, size)` — built-ins: `"montserrat"` (snaps to the nearest
enabled size: 8, 14, 16, 20, 24, 28, 32, 36, 40, 48) and `"unscii"`
(8 or 16, pixel look). Default everywhere is montserrat 14.

## Theme

`h:set_theme{}` sets per-display defaults for widgets created AFTERWARDS
(existing widgets keep their styles), layered over any firmware theme.
`screen` is applied to the active screen immediately. Each call replaces
the previous one; `h:set_theme(nil)` uninstalls. Theme state persists
across app loads — set your own at startup rather than assuming a blank
slate.

```lua
h:set_theme {
  screen = { bg_color = "#0b0b10" },
  object = { bg_opa = 0, border_width = 0 },   -- base pass, every widget
  label  = { text_color = "#ececf2", text_font = lvgl.Font("montserrat", 20) },
}
```

Class keys: `object`, `label`, `button`, `image`, `textarea`, `checkbox`,
`dropdown`, `roller`, `led`, `line`, `arc`, `scale`.

## Animation — never animate from on_tick

`on_tick` runs at ~10Hz; LVGL's animation engine runs far faster and
interpolates for you. Updaters set target values; continuous motion is
always an `Anim`:

```lua
local a = arc:Anim {
  start_value = 0, end_value = 100,
  duration = 800,                        -- ms
  path = "ease_out",                     -- "linear" | "ease_in" | "ease_out"
                                         -- | "ease_in_out" | "overshoot"
                                         -- | "bounce" | "step"
  repeat_count = lvgl.ANIM_REPEAT_INFINITE,
  exec_cb = function(obj, value) obj:set { value = value } end,
  run = true,                            -- start immediately
}
-- a:start() / a:stop(); other keys: delay, repeat_delay, playback_time,
-- playback_delay, early_apply
```

## Constants that exist

Tables of values: `lvgl.ALIGN` (`CENTER`, `TOP_LEFT`, `TOP_MID`,
`TOP_RIGHT`, `LEFT_MID`, `RIGHT_MID`, `BOTTOM_LEFT`, `BOTTOM_MID`,
`BOTTOM_RIGHT`, `OUT_*` variants), `lvgl.EVENT` (`CLICKED`, `PRESSED`,
`RELEASED`, `VALUE_CHANGED`, `READY`, `CANCEL`, ...), `lvgl.FLAG`
(`HIDDEN`, `CLICKABLE`, `CHECKABLE`, `SCROLLABLE`, `FLOATING`, ...),
`lvgl.STATE` (`CHECKED`, `DISABLED`, `PRESSED`, `FOCUSED`, ...),
`lvgl.PART`, `lvgl.TEXT_ALIGN`, `lvgl.FLEX_FLOW`, `lvgl.FLEX_ALIGN`,
`lvgl.DIR`, `lvgl.SCROLLBAR_MODE`, `lvgl.ROLLER_MODE`, `lvgl.GRAD_DIR`,
`lvgl.SCR_LOAD_ANIM`, `lvgl.BUILTIN_FONT`, `lvgl.KEY`.

Scalars and helpers: `lvgl.SIZE_CONTENT`, `lvgl.RADIUS_CIRCLE`,
`lvgl.ANIM_REPEAT_INFINITE`, `lvgl.ANIM_PLAYTIME_INFINITE`,
`lvgl.LAYOUT_FLEX`, `lvgl.LAYOUT_GRID`, `lvgl.COORD_MAX`,
`lvgl.COORD_MIN`, `lvgl.PCT(n)`, `lvgl.OPA(pct)`.

Do not invent constants beyond this list (there is no `lvgl.COLOR`, no
`lvgl.SYMBOL`, no `lvgl.ANIM.*` table).

## Rules

- Bind before touching anything else on `lvgl`.
- Build the tree in `init`; keep handles in locals/upvalues; mutate with
  `obj:set{}` from `on_tick`/`on_event`. Never re-create widgets per tick.
- No continuous motion in updaters — use `Anim`.
- Track what a frame depends on and skip `set` calls when nothing changed;
  LVGL diffs properties, but avoiding the calls is cheaper still.
- After `h.clean()` (or an app swap) every held widget handle is dead —
  drop them and rebuild.
- Deleting a widget kills its children and any running Anims on it.
