-- generation: adventure
-- Choose-your-own-adventure. The next scene requires inference, so choosing
-- escalates through arc.ask: the reflex loop acknowledges instantly (beep +
-- "thinking" indicator), the agent replies with the new scene, and a timeout
-- falls back to shipped microcopy. Shake asks the narrator to recap.

local s = arc.store{
  scene = 1,
  text  = "You wake in a moonlit orchard. A lantern glows by the gate, and something rustles in the long grass.",
  a     = "Take the lantern",
  b     = "Follow the rustle",
}

arc.tap(0, "choose_a")
arc.tap(1, "choose_b")
arc.shake(2.0, "recap")
arc.idle(120000, "bored")

local function turn(choice)
  buzzer.beep(660, 30)                       -- reflex acknowledgment
  local r = arc.ask("turn", { choice = choice, scene = s.scene }, {
    timeout_ms = 9000,
    fallback = { text = "The mist thickens and the moment passes. The orchard waits for you to choose again." },
  })
  if r.text then s.text = r.text end
  if r.a then s.a = r.a end
  if r.b then s.b = r.b end
  if r.scene then s.scene = r.scene end
  arc.play{ {520, 40}, {0, 20}, {780, 60} }  -- page-turn chime on arrival
end

arc.on("choose_a", function() turn("a") end)
arc.on("choose_b", function() turn("b") end)

arc.on("recap", function()
  local r = arc.ask("recap", { scene = s.scene }, {
    timeout_ms = 6000,
    fallback = { text = "The story so far is lost in the wind." },
  })
  if r.text then s.text = r.text end
end)

-- "bored" has no local handler: it is published upstream, where the agent
-- may decide to regenerate this app into something else entirely.

arc.view(function()
  local busy = arc.pending()
  return ui.col{ pad = 6, gap = 5,
    ui.text{ text = s.text, size = 2, w = 228, color = "white" },
    busy
      and ui.label{ text = "thinking" .. string.rep(".", arc.dots(3)),
                    size = 2, color = "amber" }
      or ui.col{ gap = 2,
           ui.label{ text = "A: " .. s.a, size = 1, color = "cyan" },
           ui.label{ text = "B: " .. s.b, size = 1, color = "cyan" },
         },
  }
end)
