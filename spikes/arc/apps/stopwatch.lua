-- generation: stopwatch
-- Pure reflex loop — after shipping, this needs zero agent involvement.
-- Button A (0) start/stop, button B (1) reset.

local s = arc.store{ running = false, elapsed = 0, laps = 0 }

arc.tap(0, "toggle")
arc.tap(1, "reset")

arc.on("toggle", function()
  s.running = not s.running
  buzzer.beep(s.running and 880 or 440, 40)
end)

arc.on("reset", function()
  s.running = false
  s.elapsed = 0
  arc.play{ {660, 40}, {0, 30}, {440, 60} }
end)

arc.every(100, function(dt)
  if s.running then s.elapsed = s.elapsed + dt end
end)

arc.view(function()
  return ui.col{ pad = 10, gap = 8, align = "center",
    ui.space{ h = 4 },
    ui.label{ text = arc.fmt_ms(s.elapsed), size = 4,
              color = s.running and "green" or "white" },
    ui.label{ text = s.running and "RUNNING" or "PAUSED",
              size = 2, color = s.running and "green" or "gray" },
    ui.label{ text = "A start/stop    B reset", size = 1, color = "dim" },
  }
end)
