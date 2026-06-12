-- skeleton.lua — stick figure from the pose model's 17 COCO keypoints.

local FRAME = 192
local MIN_SCORE = 30

-- COCO skeleton edges (1-based keypoint indices, see DEVICE-SKILL.md)
local EDGES = {
  {6, 7},               -- shoulders
  {6, 8}, {8, 10},      -- left arm
  {7, 9}, {9, 11},      -- right arm
  {6, 12}, {7, 13},     -- torso sides
  {12, 13},             -- hips
  {12, 14}, {14, 16},   -- left leg
  {13, 15}, {15, 17},   -- right leg
}

local function sy(v) return math.floor(v * screen.height() / FRAME) end
-- Mirror x: the camera faces you, so flip horizontally to make the screen
-- behave like a mirror (raise your left hand, the figure's screen-left arm rises).
local function mx(v) return math.floor((FRAME - v) * screen.width() / FRAME) end

function init(ctx)
  screen.clear()
  screen.text(10, 10, "Skeleton", 3)
  screen.text(10, 50, "needs the pose model", 2, 120, 120, 120)
  screen.flip()
end

function on_tick(ctx, dt)
  screen.clear()
  if vision.kind() ~= "pose" or vision.count() == 0 then
    screen.text(10, 50, vision.ok() and "no one in frame" or "camera offline",
                2, 120, 120, 120)
    screen.flip()
    return
  end

  for e = 1, #EDGES do
    local a = vision.keypoint(1, EDGES[e][1])
    local b = vision.keypoint(1, EDGES[e][2])
    if a and b and a.score >= MIN_SCORE and b.score >= MIN_SCORE then
      screen.line(mx(a.x), sy(a.y), mx(b.x), sy(b.y), 0, 255, 255)
    end
  end
  -- head: a dot at the nose
  local nose = vision.keypoint(1, 1)
  if nose and nose.score >= MIN_SCORE then
    screen.fill_rect(mx(nose.x) - 3, sy(nose.y) - 3, 6, 6, 255, 255, 0)
  end
  screen.flip()
end
