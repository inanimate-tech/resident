-- generation: adventure (live-demo edition)
-- Logic layer: store, recognizers, handlers, timelines — stable across looks.
-- The LOOK is swappable: arc.view(...) may be re-registered by a model-written
-- chunk the server appends after this file (shake = "remix"). Both this
-- default view and remixed views read the same globals:
--
--   S       the store: S.text, S.a, S.b, S.scene
--   TILT    smoothed accelerometer: TILT.x, TILT.y (≈ -1..1) — the lantern
--
-- Local vs remote, on purpose: tilt sways the lantern glow at 10 FPS with no
-- network involvement; button presses beep instantly (reflex) then escalate
-- as asks (response); shake regenerates the whole look (revision).

S = arc.store{
  scene = 1,
  text  = "You wake in a moonlit orchard. A lantern glows by the gate, and something rustles in the long grass.",
  a     = "Take the lantern",
  b     = "Follow the rustle",
}

TILT = { x = 0, y = 0 }
arc.every(100, function()
  local ax, ay = imu.accel()
  TILT.x = TILT.x * 0.7 + ax * 0.3
  TILT.y = TILT.y * 0.7 + ay * 0.3
end)

-- Held landscape, the top-edge side button (index 1) lines up with the top
-- choice (A); the front face button (index 0) is B.
arc.tap(1, "choose_a")
arc.tap(0, "choose_b")
arc.shake(2.0, "remix")
arc.idle(120000, "bored")

local function turn(choice)
  buzzer.beep(660, 30)                       -- reflex acknowledgment
  local r = arc.ask("turn", {
    choice = choice,
    scene  = S.scene,
    tilt   = math.floor(TILT.x * 100) / 100, -- how the lantern is held
  }, {
    timeout_ms = 9000,
    fallback = { text = "The mist thickens and the moment passes. The orchard waits for you to choose again." },
  })
  if r.text then S.text = r.text end
  if r.a then S.a = r.a end
  if r.b then S.b = r.b end
  if r.scene then S.scene = r.scene end
  arc.play{ {520, 40}, {0, 20}, {780, 60} }  -- page-turn chime on arrival
end

arc.on("choose_a", function() turn("a") end)
arc.on("choose_b", function() turn("b") end)

arc.on("remix", function()
  arc.play{ {300, 50}, {0, 30}, {300, 50} }
  -- If the server accepts, it replies by REPLACING this whole generation
  -- (new model-written view, story re-hydrated) — this ask usually dies with
  -- the app swap. The reply/fallback path only lands if the remix failed.
  arc.ask("remix", { scene = S.scene }, {
    timeout_ms = 20000,
    fallback = { note = "The world holds its shape." },
  })
end)

-- "bored" has no local handler: published upstream for the revision loop.

-- The default look. A remix chunk appended after this file re-registers
-- arc.view and this function simply stops being called.
arc.view(function()
  local glow = math.floor(120 + TILT.x * 90)
  -- arc.pending returns the ask's payload, so the wait can CONFIRM the
  -- choice: the picked option stays lit, the other dims. Local echo — the
  -- reassurance runs on-device while the slow loop thinks.
  local p = arc.pending("turn")
  return ui.col{ pad = 6, gap = 4,
    ui.canvas{ w = 228, h = 10, draw = function(x, y)
      screen.fill_rect(x, y + 8, 228, 1, 60, 50, 20)            -- horizon
      screen.fill_rect(glow - 8, y + 2, 16, 6, 255, 190, 60)    -- lantern glow
      screen.fill_rect(glow - 2, y, 4, 8, 255, 240, 180)        -- flame
    end },
    ui.text{ text = S.text, size = 1, w = 228, color = "white" },
    p and ui.col{ gap = 2,
        ui.label{ text = "A) " .. S.a, size = 1,
                  color = (p.choice == "a") and "amber" or "dim" },
        ui.label{ text = "B) " .. S.b, size = 1,
                  color = (p.choice == "b") and "amber" or "dim" },
        ui.label{ text = "the story stirs" .. string.rep(".", arc.dots(3)),
                  size = 1, color = "amber" },
      }
      or ui.col{ gap = 2,
           ui.label{ text = "A) " .. S.a, size = 1, color = "cyan" },
           ui.label{ text = "B) " .. S.b, size = 1, color = "cyan" },
         },
    arc.pending("remix")
      and ui.label{ text = "reimagining the world...", size = 1, color = "magenta" }
      or false,
  }
end)
