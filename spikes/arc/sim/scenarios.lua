-- scenarios — scripted runs of the example generations, with assertions.
-- Run from spikes/arc/:  lua sim/scenarios.lua

local sim = dofile("sim/sim.lua")

local passed, failed = 0, 0

local function check(cond, what)
  if cond then
    passed = passed + 1
  else
    failed = failed + 1
    print("  FAIL: " .. what)
  end
end

local function has(haystack, needle)
  return tostring(haystack):find(needle, 1, true) ~= nil
end

-- ---------------------------------------------------------------------------
print("scenario: stopwatch (reflex loop only)")
sim.load("apps/stopwatch.lua")

check(has(sim.texts(), "00:00.0"), "shows zero time at boot")
check(has(sim.texts(), "PAUSED"), "starts paused")
check(sim.offscreen() == nil, "stopwatch frame in bounds (" .. tostring(sim.offscreen()) .. ")")
check(sim.last_sent("arc:state") ~= nil, "hello mirror published on init")

sim.button(0)                                 -- start
check(arc._raw_store.running == true, "tap A starts the clock")
check(sim.beeped(880), "start beep is reflex (fires before any tick)")
check(has(sim.texts(), "RUNNING"), "render happens on the event, not the next tick")
local i = sim.last_sent("arc:intent")
check(i and i.name == "toggle" and i.index == 0, "intent published upstream with payload")

sim.tick(10)                                  -- 1.0s
check(arc._raw_store.elapsed == 1000, "timer accumulates elapsed (got " .. tostring(arc._raw_store.elapsed) .. ")")
check(has(sim.texts(), "00:01.0"), "time renders")

sim.button(0)                                 -- pause
sim.tick(5)
check(arc._raw_store.elapsed == 1000, "paused clock does not accumulate")

sim.button(1)                                 -- reset
check(arc._raw_store.elapsed == 0, "reset zeroes the clock")
check(sim.beeped(660), "reset chime first note is immediate")
sim.tick(1)
check(sim.beeped(440), "reset chime second note plays from the timeline")

-- ---------------------------------------------------------------------------
print("scenario: adventure (response loop: ask / reply)")
sim.load("apps/adventure.lua")

check(has(sim.texts(), "moonlit orchard"), "opening scene shows")
check(has(sim.texts(), "A: Take the lantern"), "choices show")
check(sim.offscreen() == nil, "adventure frame in bounds (" .. tostring(sim.offscreen()) .. ")")

sim.button(0)                                 -- choose A -> escalates
check(sim.beeped(660), "acknowledgment beep is reflex")
local ask = sim.last_sent("arc:ask")
check(ask ~= nil, "ask published immediately (no tick needed)")
check(ask and ask.ask == "turn" and ask.choice == "a" and ask.gen == "dev",
      "ask carries name, payload, generation id")
check(arc.pending("turn"), "ask is pending")
local pp = arc.pending("turn")
check(type(pp) == "table" and pp.choice == "a", "pending returns the ask payload for local echo")
check(has(sim.texts(), "thinking"), "fallback UI within the reflex budget")
local dots = false
for _, op in ipairs(sim.drawops) do
  if op.op == "fill_rect" and op.x >= 216 and op.y >= 128 then dots = true end
end
check(dots, "runtime draws system activity dots while an ask is pending")
check(not has(sim.texts(), "A: Take the lantern"), "choices hidden while pending")

sim.tick(3)
sim.appevent("arc:reply", { id = ask.id, text = "The lantern hums as you lift it. The gate swings open onto a starlit road.",
                            a = "Walk the road", b = "Snuff the lantern", scene = 2 })
check(not arc.pending(), "reply resolves the ask")
check(has(sim.texts(), "lantern hums"), "scene advances from the reply")
check(has(sim.texts(), "A: Walk the road"), "choices update from the reply")
check(arc._raw_store.scene == 2, "store carries the reply's state")
check(sim.beeped(520), "arrival chime")

-- timeout -> fallback
sim.button(1)                                 -- choose B, agent never replies
check(arc.pending("turn"), "second ask pending")
sim.tick(95)                                  -- 9.5s > 9s timeout
check(not arc.pending(), "timeout clears the ask")
check(has(sim.texts(), "mist thickens"), "shipped fallback text shows")
check(has(sim.texts(), "A: Walk the road"), "fallback preserves prior choices")

-- hydration + probe (revision-loop plumbing)
sim.appevent("arc:hydrate", { text = "Hydrated scene text." })
check(has(sim.texts(), "Hydrated scene text."), "hydrate merges into the store and re-renders")

local states_before = sim.sent_count("arc:state")
sim.appevent("arc:probe", {})
sim.tick(1)
check(sim.sent_count("arc:state") > states_before, "probe publishes the mirror")
local st = sim.last_sent("arc:state")
check(st and st.scene == 2 and st.gen == "dev", "mirror carries store + generation id")

-- unknown reply id is ignored, not fatal
sim.appevent("arc:reply", { id = 999, text = "stale" })
check(not has(sim.texts(), "stale"), "stale/unknown reply dropped")

-- server-sent event with no arc: prefix becomes an intent
sim.appevent("recap", {})                     -- agent injects an intent
check(arc.pending("recap"), "agent-injected event runs the same handler as a shake")
sim.appevent("arc:reply", { id = (sim.last_sent("arc:ask") or {}).id,
                            text = "So far: you took the lantern." })
check(has(sim.texts(), "So far"), "recap reply lands")

-- rate limiting: outbox holds and retries
local asks_before = sim.sent_count("arc:ask")
sim.send_ok = false
sim.button(0)
check(sim.sent_count("arc:ask") == asks_before, "rate-limited ask is queued, not sent")
check(arc.pending("turn"), "ask pending even while unsendable")
check(has(sim.texts(), "thinking"), "UI doesn't wait for the network")
sim.send_ok = true
sim.tick(1)
check(sim.sent_count("arc:ask") == asks_before + 1, "outbox retries the ask once the limiter clears")
sim.appevent("arc:reply", { id = (sim.last_sent("arc:ask") or {}).id, text = "Onward." })

-- idle recognizer fires once, upstream only
sim.tick(1250)                                -- > 120s quiet
local found_bored = false
for _, m in ipairs(sim.sent) do
  if m.name == "arc:intent" and m.data.name == "bored" then found_bored = true end
end
check(found_bored, "idle publishes 'bored' for the agent's revision loop")

-- shake recognizer
sim.accel = { 2.5, 0, 1 }
sim.tick(1)
check(arc.pending("recap"), "shake magnitude triggers the recap ask")

-- ---------------------------------------------------------------------------
print(string.format("\n%d passed, %d failed", passed, failed))
if failed > 0 then os.exit(1) end
