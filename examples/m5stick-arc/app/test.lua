-- Host-side test of the live-demo generation, using the spike's sim harness.
-- Run from this directory:  lua test.lua

local sim = dofile("../../../spikes/arc/sim/sim.lua")
local failed = 0
local function check(c, what)
  if not c then failed = failed + 1; print("  FAIL: " .. what) end
end
local function has(s, n) return tostring(s):find(n, 1, true) ~= nil end

sim.load("adventure.lua")
check(has(sim.texts(), "moonlit orchard"), "opening scene")
check(has(sim.texts(), "A) Take the lantern"), "choices marked")
check(sim.offscreen() == nil, "default view in bounds: " .. tostring(sim.offscreen()))

-- buttons are swapped for the physical hold: top-edge (1) = A, front (0) = B
sim.button(1)
local ask = sim.last_sent("arc:ask")
check(ask and ask.choice == "a" and ask.tilt ~= nil, "top-edge button asks choice A, with tilt")
check(has(sim.texts(), "the story stirs"), "authored waiting state")
check(has(sim.texts(), "A) Take the lantern"), "chosen option echoed during the wait")

-- worst-case reply length (the text-size overflow regression)
local long = string.rep("the lantern light spills over mossy stones ", 4):sub(1, 140)
sim.appevent("arc:reply", { id = ask.id, text = long, a = "Sixteen chars ok", b = "Also sixteen ok", scene = 2 })
sim.tick(1)
check(has(sim.texts(), "lantern light"), "long reply lands")
check(sim.offscreen() == nil, "140-char text in bounds: " .. tostring(sim.offscreen()))

-- tilt is reflex-only: no network traffic
local sent_before = #sim.sent
sim.accel = { 0.8, 0, 1 }
sim.tick(3)
check(#sim.sent == sent_before, "tilt causes no network traffic")

sim.button(0)
local ask2 = sim.last_sent("arc:ask")
check(ask2 and ask2.choice == "b", "front button asks choice B")
sim.appevent("arc:reply", { id = ask2.id, text = "Beat.", a = "Go", b = "Stay", scene = 3 })

-- voice: ptt on/off from the firmware, heard from the server
sim.appevent("ptt", { state = "on" })
check(has(sim.texts(), "listening"), "listening state shows while held")
sim.appevent("ptt", { state = "off" })
check(has(sim.texts(), "..."), "waiting state after release")
sim.appevent("heard", { text = "I should step closer" })
check(has(sim.texts(), '"I should step closer"'), "transcript echoed on screen")
check(sim.offscreen() == nil, "voice line in bounds")
sim.tick(70)   -- 7s: transcript line clears itself
check(not has(sim.texts(), "step closer"), "transcript clears after a few seconds")

-- a voice-routed intent arrives like any other event
sim.appevent("choose_a", { via = "voice" })
check(arc.pending("turn"), "voice-injected intent runs the normal handler")
sim.appevent("arc:reply", { id = (sim.last_sent("arc:ask") or {}).id, text = "Onward.", a = "L", b = "R", scene = 4 })

-- a free-form voice turn arrives as hydration
sim.appevent("arc:hydrate", { text = "The orchard listens.", a = "Wait", b = "Sing", scene = 5 })
check(has(sim.texts(), "The orchard listens."), "voice turn hydrates the scene")
check(arc._raw_store.scene == 5, "hydrated scene number")

-- a broken (model-written) view must not take the app down
load([[arc.view(function() return ui.col{ pad = "oops", ui.label{ text = S.text } } end)]])()
sim.tick(2)
check(#sim.logs > 0 and sim.logs[#sim.logs][1] == "error", "broken view logs an error")
sim.button(0)
check(arc.pending("turn"), "app still handles intents after a broken view")

print(failed == 0 and "ALL OK" or (failed .. " FAILURES"))
os.exit(failed == 0 and 0 or 1)
