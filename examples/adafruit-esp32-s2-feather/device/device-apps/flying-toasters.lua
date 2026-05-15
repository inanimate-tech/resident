-- Flying Toasters (After Dark 2.0 replica), animated.
--
-- Both toaster frames extracted from source images, downsampled to 32x29
-- via BOX filter (image1 = wing down, image2 = wing up). Toast bitmap
-- extracted from image1, 22x14.
--
-- Animation:
--   - Items drift down-and-left at (-1, +1) px/tick (10 FPS → ~10 px/sec).
--   - Wrap: when an item exits the left or bottom edge, it re-enters
--     from the right or top respectively. Diagonal flow.
--   - Wing flap: alternate the two toaster frames every 200 ms.

local TOASTER_DOWN = {
  "            ########            ",
  "         #####     ###          ",
  "       ####    #########        ",
  "     ###    #####      ###      ",
  "   ###    ###      ########     ",
  "  ##   ###     ############     ",
  " ##   ###    #########     #    ",
  "### ###    ##################   ",
  "#####    ###################    ",
  "######  ####################    ",
  " ###########################    ",
  "   ###### #################     ",
  " #   ### ##################     ",
  "  #   # ###################     ",
  "#  #  #####################     ",
  "   ## ####################      ",
  "   ## ###################       ",
  "   #  ##################        ",
  "      ## ################       ",
  "      # ##################### # ",
  "      #  #######################",
  "      #   ######################",
  "      #         ################",
  "#     #          ############## ",
  " #    #          ############## ",
  "  #   #       ###  ###########  ",
  "    # #    ##       #########   ",
  "      ####           ########   ",
  "                       #####    ",
}

local TOASTER_UP = {
  "        ####                    ",
  "       ######                   ",
  "      ########                  ",
  "     ######## #####             ",
  "    ###########    ###          ",
  "    #######      ######         ",
  "   ######    ####      ###      ",
  "   ###    ###        #####      ",
  "  ##    ##     #################",
  " ##   ###    ###################",
  " ##  ##    #################### ",
  "######    ##################### ",
  " ###### ######################  ",
  "  ###########################   ",
  "    ##### #################     ",
  " ##  ### ##################     ",
  "   #  # ##################      ",
  "    # ####################      ",
  "    # ###################       ",
  "      ##################        ",
  "      # ###############         ",
  "      #  #############          ",
  "      #   ##########      #     ",
  "      #     #####       ##      ",
  "#     #               #         ",
  " #    #             #           ",
  "  #   #         #               ",
  "   ## #     ###                 ",
  "     ######                     ",
}

local TOAST = {
  "          ###         ",
  "       ########       ",
  "     ############     ",
  "   ###############    ",
  " ##################   ",
  "#######     ######### ",
  "########     #########",
  " ########   ##########",
  "# ####################",
  "## ################# #",
  " ### ############# ###",
  "   ## ########## #### ",
  "     ########  ##     ",
  "     ### ### ###      ",
}

local SW, SH = 135, 240

-- Each item carries its kind, dimensions, position, per-item velocity, and
-- (for toasters) a flap phase offset so they don't flap in unison.
--
-- Angles:
--   shallow: vx/vy = -2/+1 (~26° from horizontal) — most toasters
--   steep:   vx/vy = -1.5/+1.5 (45°) — about 1/4 of toasters
-- Speeds vary roughly ±50% from the base via per-item scaling baked into vx/vy.
-- Phase offsets stagger across the 1000 ms flap cycle.

local items = {
  -- toasters (3)
  {kind=1, w=32, h=29, x= 67, y= 29, vx=-2.0, vy= 1.0, phase=  0},
  {kind=1, w=32, h=29, x=103, y= 86, vx=-1.5, vy= 1.5, phase=333},  -- 45°
  {kind=1, w=32, h=29, x=  4, y=175, vx=-2.6, vy= 1.3, phase=666},  -- fast shallow
  -- toast slices (3)
  {kind=2, w=22, h=14, x= 45, y= 61, vx=-1.8, vy= 0.9},
  {kind=2, w=22, h=14, x= 85, y=127, vx=-2.2, vy= 1.1},
  {kind=2, w=22, h=14, x=110, y=210, vx=-2.4, vy= 1.2},
}

-- Per-row RLE blit. Clips against the 135x240 screen so items mid-wrap
-- (partially off-screen) render the visible portion correctly. Floors
-- float positions to integer pixel coords.
local function blit(bmp, ox, oy)
  ox = math.floor(ox)
  oy = math.floor(oy)
  for ry = 1, #bmp do
    local y = oy + ry - 1
    if y >= 0 and y < SH then
      local row = bmp[ry]
      local len = #row
      local i = 1
      while i <= len do
        if string.sub(row, i, i) == "#" then
          local j = i + 1
          while j <= len and string.sub(row, j, j) == "#" do
            j = j + 1
          end
          local x = ox + i - 1
          local w = j - i
          if x < 0 then
            w = w + x
            x = 0
          end
          if x + w > SW then
            w = SW - x
          end
          if w > 0 then
            screen.fill_rect(x, y, w, 1, 255, 255, 255)
          end
          i = j
        else
          i = i + 1
        end
      end
    end
  end
end

function init(ctx)
  -- nothing — on_tick handles all drawing
end

function on_tick(ctx, dt_ms)
  for _, it in ipairs(items) do
    it.x = it.x + it.vx
    it.y = it.y + it.vy
    if it.x < -it.w then it.x = it.x + SW + it.w end
    if it.y > SH      then it.y = it.y - SH - it.h end
  end

  screen.clear()

  for _, it in ipairs(items) do
    if it.kind == 1 then
      -- 1 Hz flap cycle: 500 ms per frame, alternating. Per-toaster phase
      -- offset keeps them out of sync.
      local up = math.floor((ctx.time_ms + it.phase) / 500) % 2 == 0
      if up then
        blit(TOASTER_UP, it.x, it.y)
      else
        blit(TOASTER_DOWN, it.x, it.y + 1)
      end
    else
      blit(TOAST, it.x, it.y)
    end
  end

  screen.flip()
end
