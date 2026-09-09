# Grove Vision AI V2 Example Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** New example `examples/m5stick-grove-vision-ai` — M5StickS3 + Grove Vision AI V2 (I2C), with a model-agnostic `GroveVisionDriver` that feeds inference results into the Resident sandbox as condensed events plus a pollable `vision.*` Lua module, with verbose serial logging of everything the module emits.

**Architecture:** A header-only pure helper (`vision_frame.h`) holds the frame structs, kind classification, and best-detection selection — natively unit-testable with no Arduino deps. `GroveVisionDriver` (a `Resident::Driver`) polls the module via the Seeed SSCMA library at ~5 Hz from `update()`, copies results into a `vision::Frame`, emits one condensed `"vision"` event per frame-with-detections (plus empty-transition and link events), exposes the cached frame to Lua, and logs full frame detail to serial. The example project copies m5stick-demo's scaffold, symlinks its shared drivers, and targets the S3 only.

**Tech Stack:** PlatformIO (espressif32@6.12.0 / Arduino), `seeed-studio/Seeed_Arduino_SSCMA@^1.0`, M5Unified, Resident (in-tree symlink), Unity native tests.

**Spec:** `.claude/superpowers/specs/2026-06-12-grove-vision-ai-example-design.md`

**⚠️ Commit policy for this plan (house rule overrides "commit every task"):** firmware under `examples/*/src|lib` and `platformio.ini` is NOT committed until Matt has bench-verified it on the device. Tasks below build up the working tree; commits happen at the two marked CHECKPOINT gates. Native-test-only steps still run tests as they go.

**Branch:** `spike/grove` (already created, current HEAD = main's 087dd07).

**Reference numbers used throughout:** Grove I2C on M5StickS3: SDA=GPIO9, SCL=GPIO10. Module I2C addr 0x62 (SSCMA lib default). SSCMA result types: `boxes_t{uint16 x,y,w,h; uint8 score,target}`, `classes_t{uint8 target,score}`, `point_t{uint16 x,y,z; uint8 score,target}`, `keypoints_t{boxes_t box; vector<point_t> points}` (pose = 17 COCO points). Scores 0–100. Coordinates = model-frame pixels (typically 192×192).

---

### Task 1: Scaffold the example project (copy m5stick-demo, S3-only)

**Files:**
- Create: `examples/m5stick-grove-vision-ai/device/platformio.ini`
- Create: `examples/m5stick-grove-vision-ai/device/src/main.cpp` (vision driver added in Task 4)
- Create: `examples/m5stick-grove-vision-ai/send-app.sh` (byte-copy)
- Create: `examples/m5stick-grove-vision-ai/.gitignore`

- [ ] **Step 1: Copy the unchanged pieces**

```bash
cd /Users/matt/code/resident
mkdir -p examples/m5stick-grove-vision-ai/device/src examples/m5stick-grove-vision-ai/device-apps
cp examples/m5stick-demo/send-app.sh examples/m5stick-grove-vision-ai/send-app.sh
printf '.pio/\n.resident-device-id\n' > examples/m5stick-grove-vision-ai/.gitignore
```

(No `partitions.csv`: m5stick-demo's S3 env doesn't use one — only the C Plus2 env does.)

- [ ] **Step 2: Write `device/platformio.ini`**

S3-only firmware env plus a standalone native env for the helper tests. The native env deliberately does NOT inherit a base `[env]` section — that's why there is none.

```ini
[platformio]
default_envs = m5sticks3

[env:m5sticks3]
platform = espressif32@6.12.0
framework = arduino
board = esp32-s3-devkitc-1
board_build.flash_size = 8MB
board_build.arduino.memory_type = qio_opi
monitor_speed = 115200
lib_deps =
    ; resident: symlink to in-tree source (repo root, three levels up).
    symlink://../../..
    ; PIO quirk: symlinking a parent that contains this project suppresses PIO's
    ; auto-scan of our own lib/, so re-add local libs explicitly.
    symlink://lib/vision
    ; shared M5Stick drivers (display/imu/buzzer/buttons) live in m5stick-demo.
    symlink://../../m5stick-demo/device/lib/drivers
    git+https://github.com/inanimate-tech/courier.git
    tzapu/WiFiManager@^2.0.17
    bblanchon/ArduinoJson@^7.4.2
    ropg/ezTime@^0.8.3
    fischer-simon/Esp32Lua@^5.4.7
    m5stack/M5Unified
    M5PM1=https://github.com/m5stack/M5PM1
    seeed-studio/Seeed_Arduino_SSCMA@^1.0
; Resident headers use std::optional (C++17). arduino-espressif32 6.12
; defaults to gnu++11, so override.
build_unflags = -std=gnu++11
build_flags =
    -std=gnu++17
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DBOARD_M5STICKS3

; Native unit tests for the pure vision_frame helper (Task 2). The test
; includes vision_frame.h by relative path, so no lib_deps are needed and
; the SSCMA-dependent driver code is never compiled natively.
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17
```

- [ ] **Step 3: Write `device/src/main.cpp`**

m5stick-demo's main, S3-only (drop the C Plus2 pin branch), renamed deviceType. The vision driver is added in Task 4 — this compiles without it first.

```cpp
#include <M5Unified.h>
#include <Resident.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"

// Default endpoint: the canonical Resident relay. Devs can self-host by
// changing RESIDENT_HOST below (or extending Courier with a config portal).
// The relay speaks the Resident canonical protocol:
//   wss://<host>/devices/<deviceId>            ← device WS (here)
//   POST https://<host>/devices/<deviceId>/send  ← skill/curl pushes JSON
static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

// M5StickS3 buttons (ESP32-S3 with OPI PSRAM): GPIO 11 + 12. GPIO 37 is part
// of the OPI PSRAM interface — reading it via digitalRead() triggers a
// watchdog reset, which is why the C Plus2 pin map doesn't apply here.
static constexpr uint8_t BUTTON_PINS[] = {11, 12};
static constexpr PushButtonsConfig buttonConfig = {.numButtons = 2, .pins = BUTTON_PINS};

DisplayDriver displayDriver;
IMUDriver imuDriver;
BuzzerDriver buzzerDriver{255};
PushButtonsDriver buttonDriver{buttonConfig};

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "m5stick-vision";
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver, &buttonDriver};
    cfg.statusDisplay = &displayDriver;

    // Courier::Config has a constructor with default args, so designated
    // initializers (.host = ...) don't compile under strict ESP-IDF builds.
    // Use direct field assignment.
    Courier::Config courier;
    courier.host = RESIDENT_HOST;
    courier.port = RESIDENT_PORT;
    cfg.network  = courier;

    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for USB CDC on M5StickS3
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);

    // Override the default /agents/<type>-agent/<deviceId> path with the
    // canonical /devices/<deviceId> path used by resident.inanimate.tech.
    sandbox.onTransportsWillConnect([]() {
        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
    });

    // On first successful connection, replace the StatusDisplay's "Connected"
    // text with a sandbox app that shows the device ID prominently (so the
    // user knows what to push to). A real app sent via push-app or
    // send-app.sh will replace this.
    sandbox.onConnected([]() {
        static bool loaded = false;
        if (loaded) return;
        loaded = true;
        String app = "function init(ctx)\n"
                     "  screen.clear()\n"
                     "  screen.text(10, 15, 'Resident', 3)\n"
                     "  screen.text(10, 60, 'Device ID:', 2)\n"
                     "  screen.text(10, 90, '";
        app += sandbox.getDeviceId();
        app += "', 3, 0, 255, 0)\n"
               "  screen.flip()\n"
               "end\n";
        sandbox.loadApp(app.c_str());
    });

    sandbox.setup();
}

void loop() {
    M5.update();
    sandbox.loop();
}
```

- [ ] **Step 4: Create the lib placeholder so the symlink resolves**

```bash
mkdir -p examples/m5stick-grove-vision-ai/device/lib/vision/src
cat > examples/m5stick-grove-vision-ai/device/lib/vision/library.json <<'EOF'
{
  "name": "GroveVisionDriver",
  "version": "0.1.0"
}
EOF
```

- [ ] **Step 5: Compile-check the scaffold**

Run: `cd examples/m5stick-grove-vision-ai/device && pio run -e m5sticks3`
Expected: `SUCCESS` (lib/vision is an empty lib at this point — fine).

**No commit yet — firmware gate at Task 5's checkpoint.**

---

### Task 2: `vision_frame.h` pure helper (TDD, native)

**Files:**
- Create: `examples/m5stick-grove-vision-ai/device/lib/vision/src/vision_frame.h`
- Test: `examples/m5stick-grove-vision-ai/device/test/test_vision_frame/test_vision_frame.cpp`

Header-only and Arduino-free on purpose: the native test includes it by
relative path, so PlatformIO's LDF never tries to compile the
SSCMA-dependent driver under `platform = native`.

- [ ] **Step 1: Write the failing test**

```cpp
// examples/m5stick-grove-vision-ai/device/test/test_vision_frame/test_vision_frame.cpp
#include <unity.h>
#include "../../lib/vision/src/vision_frame.h"

void setUp(void) {}
void tearDown(void) {}

void test_classify_precedence(void) {
    using vision::Kind;
    // keypoints beat boxes beat points beat classes
    TEST_ASSERT_EQUAL(Kind::Pose,    vision::classify(1, 2, 3, 4));
    TEST_ASSERT_EQUAL(Kind::Boxes,   vision::classify(0, 2, 3, 4));
    TEST_ASSERT_EQUAL(Kind::Points,  vision::classify(0, 0, 3, 4));
    TEST_ASSERT_EQUAL(Kind::Classes, vision::classify(0, 0, 0, 4));
    TEST_ASSERT_EQUAL(Kind::None,    vision::classify(0, 0, 0, 0));
}

void test_kind_name(void) {
    TEST_ASSERT_EQUAL_STRING("pose",    vision::kindName(vision::Kind::Pose));
    TEST_ASSERT_EQUAL_STRING("boxes",   vision::kindName(vision::Kind::Boxes));
    TEST_ASSERT_EQUAL_STRING("points",  vision::kindName(vision::Kind::Points));
    TEST_ASSERT_EQUAL_STRING("classes", vision::kindName(vision::Kind::Classes));
    TEST_ASSERT_EQUAL_STRING("none",    vision::kindName(vision::Kind::None));
}

void test_frame_count_follows_kind(void) {
    vision::Frame f;
    f.boxes = {{10, 20, 30, 40, 90, 1}, {50, 60, 70, 80, 70, 0}};
    f.classes = {{0, 99}};
    f.kind = vision::Kind::Boxes;
    TEST_ASSERT_EQUAL_INT(2, f.count());
    f.kind = vision::Kind::Classes;
    TEST_ASSERT_EQUAL_INT(1, f.count());
    f.kind = vision::Kind::None;
    TEST_ASSERT_EQUAL_INT(0, f.count());
}

void test_best_index_picks_highest_score(void) {
    vision::Frame f;
    f.kind = vision::Kind::Boxes;
    f.boxes = {{0, 0, 1, 1, 50, 0}, {0, 0, 1, 1, 95, 2}, {0, 0, 1, 1, 70, 1}};
    TEST_ASSERT_EQUAL_INT(1, vision::bestIndex(f));
}

void test_best_index_pose_uses_box_score(void) {
    vision::Frame f;
    f.kind = vision::Kind::Pose;
    vision::Person a; a.box = {0, 0, 1, 1, 40, 0};
    vision::Person b; b.box = {0, 0, 1, 1, 88, 0};
    f.people = {a, b};
    TEST_ASSERT_EQUAL_INT(1, vision::bestIndex(f));
}

void test_best_index_empty_is_minus_one(void) {
    vision::Frame f;
    TEST_ASSERT_EQUAL_INT(-1, vision::bestIndex(f));
    f.kind = vision::Kind::Boxes;  // kind set but vector empty
    TEST_ASSERT_EQUAL_INT(-1, vision::bestIndex(f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_classify_precedence);
    RUN_TEST(test_kind_name);
    RUN_TEST(test_frame_count_follows_kind);
    RUN_TEST(test_best_index_picks_highest_score);
    RUN_TEST(test_best_index_pose_uses_box_score);
    RUN_TEST(test_best_index_empty_is_minus_one);
    UNITY_END();
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd examples/m5stick-grove-vision-ai/device && pio test -e native`
Expected: FAIL — `vision_frame.h: No such file or directory`.

- [ ] **Step 3: Write `lib/vision/src/vision_frame.h`**

```cpp
// vision_frame.h — pure data model for Grove Vision AI V2 results.
//
// Header-only and Arduino-free so it runs under native unit tests. The
// driver copies SSCMA's result vectors into these structs once per invoke;
// everything downstream (events, Lua module, logging) reads this frame.
//
// Units: coordinates are integer pixels in the model's input frame
// (typically 192×192) exactly as SSCMA reports them; scores are 0–100.
#pragma once
#include <cstdint>
#include <vector>

namespace vision {

struct Box {
  uint16_t x = 0, y = 0, w = 0, h = 0;
  uint8_t score = 0, target = 0;
};

struct Point {
  uint16_t x = 0, y = 0, z = 0;
  uint8_t score = 0, target = 0;
};

struct Classification {
  uint8_t target = 0, score = 0;
};

struct Person {
  Box box;
  std::vector<Point> points;  // COCO order; pose models emit 17
};

enum class Kind { None, Pose, Boxes, Points, Classes };

// One model emits one result type per invoke; the precedence only matters
// defensively (richer kinds win if a model ever emits several).
inline Kind classify(size_t nPeople, size_t nBoxes, size_t nPoints, size_t nClasses) {
  if (nPeople > 0)  return Kind::Pose;
  if (nBoxes > 0)   return Kind::Boxes;
  if (nPoints > 0)  return Kind::Points;
  if (nClasses > 0) return Kind::Classes;
  return Kind::None;
}

inline const char* kindName(Kind k) {
  switch (k) {
    case Kind::Pose:    return "pose";
    case Kind::Boxes:   return "boxes";
    case Kind::Points:  return "points";
    case Kind::Classes: return "classes";
    default:            return "none";
  }
}

struct Frame {
  Kind kind = Kind::None;
  std::vector<Box> boxes;
  std::vector<Point> points;
  std::vector<Classification> classes;
  std::vector<Person> people;

  int count() const {
    switch (kind) {
      case Kind::Pose:    return (int)people.size();
      case Kind::Boxes:   return (int)boxes.size();
      case Kind::Points:  return (int)points.size();
      case Kind::Classes: return (int)classes.size();
      default:            return 0;
    }
  }
};

// Index of the highest-score detection for the frame's kind; -1 if none.
// Pose people rank by their box score.
inline int bestIndex(const Frame& f) {
  int best = -1;
  int bestScore = -1;
  int n = f.count();
  for (int i = 0; i < n; i++) {
    int s = -1;
    switch (f.kind) {
      case Kind::Pose:    s = f.people[i].box.score; break;
      case Kind::Boxes:   s = f.boxes[i].score; break;
      case Kind::Points:  s = f.points[i].score; break;
      case Kind::Classes: s = f.classes[i].score; break;
      default: break;
    }
    if (s > bestScore) { bestScore = s; best = i; }
  }
  return best;
}

}  // namespace vision
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd examples/m5stick-grove-vision-ai/device && pio test -e native`
Expected: `6 test cases: 6 succeeded`.

**No commit yet — gate at Task 5's checkpoint.**

---

### Task 3: GroveVisionDriver (poll, events, verbose logging)

**Files:**
- Create: `examples/m5stick-grove-vision-ai/device/lib/vision/src/GroveVisionDriver.h`
- Create: `examples/m5stick-grove-vision-ai/device/lib/vision/src/GroveVisionDriver.cpp`

- [ ] **Step 1: Write `GroveVisionDriver.h`**

```cpp
#ifndef GROVE_VISION_DRIVER_H
#define GROVE_VISION_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <Seeed_Arduino_SSCMA.h>
#include "vision_frame.h"

struct GroveVisionConfig {
  uint8_t sdaPin = 9;        // M5StickS3 Grove port
  uint8_t sclPin = 10;
  uint32_t pollMs = 200;     // ~5 Hz invoke rate
  bool verboseLog = true;    // per-frame serial dump of everything received
};

// Resident driver for the Grove Vision AI V2 module (I2C addr 0x62, SSCMA
// protocol, stock SenseCraft firmware). Model-agnostic: whatever model is
// flashed via SenseCraft, results arrive as one of four SSCMA types and map
// onto vision::Frame.
//
// Lua API (module "vision"):
//   vision.kind()          -> "pose"|"boxes"|"classes"|"points"|"none"
//   vision.count()         -> detections in the last frame
//   vision.detection(i)    -> table (1-based; fields per kind) or nil
//   vision.keypoint(i, k)  -> {x,y,score} for pose person i, COCO point k (1..17), or nil
//   vision.age_ms()        -> ms since the last successful invoke
//   vision.ok()            -> true when the module link is up
//
// Events (sendEvent name "vision"):
//   frame with detections: kind, n, target, score, x, y, w, h
//                          (classes: no box fields; points: x,y,z)
//   transition to empty:   kind, n=0   (once, not per empty frame)
//   link up/down:          kind="link", ok=1|0
class GroveVisionDriver : public Resident::Driver {
public:
  explicit GroveVisionDriver(const GroveVisionConfig& config = {})
      : _config(config) {}

  const char* name() const override { return "vision"; }
  void begin() override;
  void update() override;
  void registerModule(Resident::LuaModule& m) override {
    m.method<GroveVisionDriver, &GroveVisionDriver::luaKind>("kind")
     .method<GroveVisionDriver, &GroveVisionDriver::luaCount>("count")
     .method<GroveVisionDriver, &GroveVisionDriver::luaDetection>("detection")
     .method<GroveVisionDriver, &GroveVisionDriver::luaKeypoint>("keypoint")
     .method<GroveVisionDriver, &GroveVisionDriver::luaAgeMs>("age_ms")
     .method<GroveVisionDriver, &GroveVisionDriver::luaOk>("ok");
  }

  int luaKind(lua_State* L);
  int luaCount(lua_State* L);
  int luaDetection(lua_State* L);
  int luaKeypoint(lua_State* L);
  int luaAgeMs(lua_State* L);
  int luaOk(lua_State* L);

private:
  void pollModule();
  void copyResults();
  void emitFrameEvent();
  void setLink(bool up);
  void logModelInfo();
  void logFrame();

  GroveVisionConfig _config;
  SSCMA _ai;
  vision::Frame _frame;

  bool _linkUp = false;
  bool _beginOk = false;
  uint8_t _failStreak = 0;
  unsigned long _lastPollMs = 0;
  unsigned long _lastFrameMs = 0;   // last successful invoke
  int _lastCount = 0;               // for the transition-to-empty event

  static constexpr uint8_t FAILS_TO_LINK_DOWN = 3;
  static constexpr uint32_t RETRY_WHEN_DOWN_MS = 2000;
};

#endif  // GROVE_VISION_DRIVER_H
```

- [ ] **Step 2: Write `GroveVisionDriver.cpp`**

```cpp
#include "GroveVisionDriver.h"

#include <Arduino.h>
#include <Wire.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

namespace {
// EventField helpers — the union member init is fiddly inline.
void setInt(Resident::EventField& f, const char* key, int v) {
  f.key = key;
  f.type = Resident::EventField::INT;
  f.i = v;
}
void setStr(Resident::EventField& f, const char* key, const char* s) {
  f.key = key;
  f.type = Resident::EventField::STRING;
  f.s = s;
}
}  // namespace

void GroveVisionDriver::begin() {
  Wire.begin(_config.sdaPin, _config.sclPin);
  _beginOk = _ai.begin(&Wire);
  Serial.printf("[vision] begin: SDA=%d SCL=%d -> %s\n",
                _config.sdaPin, _config.sclPin,
                _beginOk ? "module found" : "MODULE OFFLINE (will retry)");
  if (_beginOk) {
    setLink(true);
    logModelInfo();
  }
}

void GroveVisionDriver::update() {
  unsigned long now = millis();
  uint32_t interval = _linkUp ? _config.pollMs : RETRY_WHEN_DOWN_MS;
  if (now - _lastPollMs < interval) return;
  _lastPollMs = now;

  if (!_beginOk) {
    // begin() failed entirely (module absent at boot) — retry the handshake.
    _beginOk = _ai.begin(&Wire);
    if (!_beginOk) return;
  }

  pollModule();
}

void GroveVisionDriver::pollModule() {
  if (_ai.invoke(1, false, false) != CMD_OK) {
    if (_linkUp && ++_failStreak >= FAILS_TO_LINK_DOWN) {
      setLink(false);
    } else if (_config.verboseLog) {
      Serial.printf("[vision] invoke failed (%d/%d)\n",
                    _failStreak, FAILS_TO_LINK_DOWN);
    }
    return;
  }

  _failStreak = 0;
  _lastFrameMs = millis();
  if (!_linkUp) {
    setLink(true);
    logModelInfo();  // model may have been re-flashed while away
  }

  copyResults();
  if (_config.verboseLog) logFrame();
  emitFrameEvent();
  _lastCount = _frame.count();
}

void GroveVisionDriver::copyResults() {
  _frame = vision::Frame{};

  for (const auto& b : _ai.boxes()) {
    _frame.boxes.push_back({b.x, b.y, b.w, b.h, b.score, b.target});
  }
  for (const auto& c : _ai.classes()) {
    _frame.classes.push_back({c.target, c.score});
  }
  for (const auto& p : _ai.points()) {
    _frame.points.push_back({p.x, p.y, p.z, p.score, p.target});
  }
  for (const auto& k : _ai.keypoints()) {
    vision::Person person;
    person.box = {k.box.x, k.box.y, k.box.w, k.box.h, k.box.score, k.box.target};
    for (const auto& p : k.points) {
      person.points.push_back({p.x, p.y, p.z, p.score, p.target});
    }
    _frame.people.push_back(std::move(person));
  }

  _frame.kind = vision::classify(_frame.people.size(), _frame.boxes.size(),
                                 _frame.points.size(), _frame.classes.size());
}

void GroveVisionDriver::emitFrameEvent() {
  int n = _frame.count();
  if (n == 0) {
    if (_lastCount > 0) {  // transition to empty, once
      Resident::EventField f[2];
      setStr(f[0], "kind", vision::kindName(_frame.kind));
      setInt(f[1], "n", 0);
      sendEvent("vision", f, 2);
    }
    return;
  }

  int best = vision::bestIndex(_frame);
  Resident::EventField f[8];
  setStr(f[0], "kind", vision::kindName(_frame.kind));
  setInt(f[1], "n", n);

  switch (_frame.kind) {
    case vision::Kind::Classes: {
      const auto& c = _frame.classes[best];
      setInt(f[2], "target", c.target);
      setInt(f[3], "score", c.score);
      sendEvent("vision", f, 4);
      break;
    }
    case vision::Kind::Points: {
      const auto& p = _frame.points[best];
      setInt(f[2], "target", p.target);
      setInt(f[3], "score", p.score);
      setInt(f[4], "x", p.x);
      setInt(f[5], "y", p.y);
      setInt(f[6], "z", p.z);
      sendEvent("vision", f, 7);
      break;
    }
    default: {  // Boxes and Pose both lead with a box
      const vision::Box& b = (_frame.kind == vision::Kind::Pose)
                                 ? _frame.people[best].box
                                 : _frame.boxes[best];
      setInt(f[2], "target", b.target);
      setInt(f[3], "score", b.score);
      setInt(f[4], "x", b.x);
      setInt(f[5], "y", b.y);
      setInt(f[6], "w", b.w);
      setInt(f[7], "h", b.h);
      sendEvent("vision", f, 8);
      break;
    }
  }
}

void GroveVisionDriver::setLink(bool up) {
  if (_linkUp == up) return;
  _linkUp = up;
  _failStreak = 0;
  Serial.printf("[vision] link %s\n", up ? "UP" : "DOWN");
  Resident::EventField f[2];
  setStr(f[0], "kind", "link");
  setInt(f[1], "ok", up ? 1 : 0);
  sendEvent("vision", f, 2);
}

void GroveVisionDriver::logModelInfo() {
  // name() and info() come from the module's SenseCraft metadata. info() is
  // raw JSON including the model's class-label table — exactly what you need
  // when eyeballing an unfamiliar model's targets.
  char* n = _ai.name(false);
  Serial.printf("[vision] model name: %s\n", n ? n : "(null)");
  String info = _ai.info(false);
  Serial.printf("[vision] model info: %s\n", info.c_str());
}

void GroveVisionDriver::logFrame() {
  int n = _frame.count();
  if (n == 0) {
    if (_lastCount > 0) Serial.println("[vision] frame empty");
    return;  // stay quiet while the scene remains empty
  }

  auto& perf = _ai.perf();
  Serial.printf("[vision] kind=%s n=%d perf=%u/%u/%ums\n",
                vision::kindName(_frame.kind), n,
                perf.prepocess, perf.inference, perf.postprocess);

  switch (_frame.kind) {
    case vision::Kind::Boxes:
      for (int i = 0; i < n; i++) {
        const auto& b = _frame.boxes[i];
        Serial.printf("  box[%d] target=%u score=%u x=%u y=%u w=%u h=%u\n",
                      i, b.target, b.score, b.x, b.y, b.w, b.h);
      }
      break;
    case vision::Kind::Classes:
      for (int i = 0; i < n; i++) {
        const auto& c = _frame.classes[i];
        Serial.printf("  class[%d] target=%u score=%u\n", i, c.target, c.score);
      }
      break;
    case vision::Kind::Points:
      for (int i = 0; i < n; i++) {
        const auto& p = _frame.points[i];
        Serial.printf("  point[%d] target=%u score=%u x=%u y=%u z=%u\n",
                      i, p.target, p.score, p.x, p.y, p.z);
      }
      break;
    case vision::Kind::Pose:
      for (int i = 0; i < n; i++) {
        const auto& person = _frame.people[i];
        const auto& b = person.box;
        Serial.printf("  person[%d] score=%u box=(%u,%u %ux%u) kp:",
                      i, b.score, b.x, b.y, b.w, b.h);
        for (size_t k = 0; k < person.points.size(); k++) {
          const auto& p = person.points[k];
          Serial.printf(" %zu:(%u,%u,%u)", k, p.x, p.y, p.score);
        }
        Serial.println();
      }
      break;
    default:
      break;
  }
}

// --- Lua bindings ---

int GroveVisionDriver::luaKind(lua_State* L) {
  lua_pushstring(L, vision::kindName(_frame.kind));
  return 1;
}

int GroveVisionDriver::luaCount(lua_State* L) {
  lua_pushinteger(L, _frame.count());
  return 1;
}

int GroveVisionDriver::luaDetection(lua_State* L) {
  int i = (int)luaL_optinteger(L, 1, 1) - 1;  // 1-based from Lua
  if (i < 0 || i >= _frame.count()) {
    lua_pushnil(L);
    return 1;
  }

  lua_newtable(L);
  auto setField = [L](const char* k, int v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, k);
  };

  switch (_frame.kind) {
    case vision::Kind::Classes: {
      const auto& c = _frame.classes[i];
      setField("target", c.target);
      setField("score", c.score);
      break;
    }
    case vision::Kind::Points: {
      const auto& p = _frame.points[i];
      setField("target", p.target);
      setField("score", p.score);
      setField("x", p.x);
      setField("y", p.y);
      setField("z", p.z);
      break;
    }
    case vision::Kind::Pose:
    case vision::Kind::Boxes: {
      const vision::Box& b = (_frame.kind == vision::Kind::Pose)
                                 ? _frame.people[i].box
                                 : _frame.boxes[i];
      setField("target", b.target);
      setField("score", b.score);
      setField("x", b.x);
      setField("y", b.y);
      setField("w", b.w);
      setField("h", b.h);
      break;
    }
    default:
      break;
  }
  return 1;
}

int GroveVisionDriver::luaKeypoint(lua_State* L) {
  int i = (int)luaL_optinteger(L, 1, 1) - 1;
  int k = (int)luaL_optinteger(L, 2, 1) - 1;
  if (_frame.kind != vision::Kind::Pose ||
      i < 0 || i >= (int)_frame.people.size() ||
      k < 0 || k >= (int)_frame.people[i].points.size()) {
    lua_pushnil(L);
    return 1;
  }
  const auto& p = _frame.people[i].points[k];
  lua_newtable(L);
  lua_pushinteger(L, p.x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, p.y);
  lua_setfield(L, -2, "y");
  lua_pushinteger(L, p.score);
  lua_setfield(L, -2, "score");
  return 1;
}

int GroveVisionDriver::luaAgeMs(lua_State* L) {
  lua_pushinteger(L, (lua_Integer)(millis() - _lastFrameMs));
  return 1;
}

int GroveVisionDriver::luaOk(lua_State* L) {
  lua_pushboolean(L, _linkUp);
  return 1;
}
```

- [ ] **Step 3: Compile-check**

Run: `cd examples/m5stick-grove-vision-ai/device && pio run -e m5sticks3`
Expected: `SUCCESS`. If `EventField` member access fails to compile (the union is anonymous in `ResidentDriver.h`), adjust the `setInt`/`setStr` helpers to match the actual struct — check `src/ResidentDriver.h:8-16`.

- [ ] **Step 4: Re-run native tests (must still pass)**

Run: `cd examples/m5stick-grove-vision-ai/device && pio test -e native`
Expected: `6 test cases: 6 succeeded` (proves the driver didn't leak Arduino deps into vision_frame.h).

**No commit yet — gate at Task 5's checkpoint.**

---

### Task 4: Wire the driver into main.cpp

**Files:**
- Modify: `examples/m5stick-grove-vision-ai/device/src/main.cpp`

- [ ] **Step 1: Add the driver**

Three edits to the Task 1 main.cpp:

After `#include "PushButtonsDriver.h"`:
```cpp
#include "GroveVisionDriver.h"
```

After `PushButtonsDriver buttonDriver{buttonConfig};`:
```cpp
// Grove Vision AI V2 on the Grove port (I2C). Defaults: SDA=9, SCL=10,
// 5 Hz poll, verbose serial logging of every frame (the point of this spike
// is eyeballing what each SenseCraft model emits).
GroveVisionDriver visionDriver;
```

In `makeConfig()`, replace the extensions line with:
```cpp
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver,
                         &buttonDriver, &visionDriver};
```

- [ ] **Step 2: Build**

Run: `cd examples/m5stick-grove-vision-ai/device && pio run -e m5sticks3`
Expected: `SUCCESS`.

---

### Task 5: ⚙️ CHECKPOINT — bench verification, then first commit

- [ ] **Step 1: Hand to Matt for bench test**

Flash and watch serial:
```bash
cd examples/m5stick-grove-vision-ai/device
pio run -e m5sticks3 -t upload && pio device monitor
```

With the Vision AI V2 on the Grove port and (at least) a detection model
flashed via SenseCraft, expect:
- `[vision] begin: SDA=9 SCL=10 -> module found`
- `[vision] model name: …` and `[vision] model info: {…}` (label table visible)
- `[vision] link UP`
- Per-frame lines when something is in view, e.g.
  `[vision] kind=boxes n=1 perf=…` + `  box[0] target=0 score=87 x=… y=… w=… h=…`
- `[vision] frame empty` once when the scene clears
- Unplug the module → `[vision] link DOWN` within ~3 polls; replug → `link UP`
  + model info again.

**STOP. Do not commit until Matt confirms the above on hardware.** Iterate on
the driver if reality disagrees with the plan (likely candidates: SSCMA
`name()`/`info()` content, perf availability, invoke latency).

- [ ] **Step 2: Commit firmware (after Matt's confirmation)**

```bash
cd /Users/matt/code/resident
git add examples/m5stick-grove-vision-ai/
git commit -m "feat(examples): m5stick-grove-vision-ai — GroveVisionDriver for Grove Vision AI V2

New S3-only example pairing the M5StickS3 with Seeed's Grove Vision AI V2
module over Grove I2C (SSCMA protocol, stock SenseCraft firmware).
GroveVisionDriver polls at 5 Hz, normalizes the four SSCMA result types
(boxes/classes/points/keypoints) into a cached frame, emits condensed
\"vision\" events (best detection + count, empty/link transitions), exposes
full detail to Lua via vision.* (kind/count/detection/keypoint/age_ms/ok),
and verbose-logs every frame to serial for model eyeballing. Pure frame
helper is header-only with native unit tests.

Verified on hardware: <model(s) Matt tested with>.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: DEVICE-SKILL.md (Lua app authors only)

**Files:**
- Create: `examples/m5stick-grove-vision-ai/DEVICE-SKILL.md`

- [ ] **Step 1: Copy the m5stick-demo skill and adapt**

```bash
cp examples/m5stick-demo/DEVICE-SKILL.md examples/m5stick-grove-vision-ai/DEVICE-SKILL.md
```

Then: in the intro paragraph and `## Hardware` section, state this is the
**M5StickS3** (not C Plus2) with a **Grove Vision AI V2 camera module** on the
Grove port; keep the screen/imu/buzzer/button sections verbatim (same shared
drivers); delete any C Plus2-specific notes.

- [ ] **Step 2: Add the `vision.*` module section after `### button.*`**

Insert exactly:

````markdown
### vision.*

A Grove Vision AI V2 camera module runs a SenseCraft AI model on-device and
this module exposes the latest inference results. Which *kind* of result you
get depends on the model currently flashed on the camera module — write apps
against `vision.kind()`:

| kind        | models                                            | detection fields        |
|-------------|---------------------------------------------------|-------------------------|
| `"boxes"`   | person/face/gesture(rock-paper-scissors)/object detection | `x,y,w,h,score,target` |
| `"classes"` | classification (no location)                      | `score,target`          |
| `"pose"`    | human pose (YOLOv8)                               | `x,y,w,h,score,target` + 17 keypoints |
| `"points"`  | point models                                      | `x,y,z,score,target`    |
| `"none"`    | nothing detected yet / no frame                   | —                       |

Coordinates are pixels in the **model's frame** (typically 192×192), NOT
screen pixels — scale before drawing. Scores are 0–100. `target` is an
integer class index; the mapping to labels (e.g. gesture model: 0=paper,
1=rock, 2=scissors) depends on the flashed model.

```lua
vision.kind()              -- current result kind (string, see table)
vision.count()             -- number of detections in the latest frame
vision.detection(i)        -- 1-based; table of fields per kind, or nil
vision.keypoint(i, k)      -- pose only: person i, keypoint k (1..17) -> {x,y,score} or nil
vision.age_ms()            -- ms since the camera last answered (big = stale)
vision.ok()                -- false when the camera module is unreachable
```

COCO keypoints (k is 1-based): 1 nose, 2 l-eye, 3 r-eye, 4 l-ear, 5 r-ear,
6 l-shoulder, 7 r-shoulder, 8 l-elbow, 9 r-elbow, 10 l-wrist, 11 r-wrist,
12 l-hip, 13 r-hip, 14 l-knee, 15 r-knee, 16 l-ankle, 17 r-ankle.

New inference arrives ~5×/sec; `on_tick` (10/sec) sees each frame about
twice. Apps also receive **events** (`on_event`) named `"vision"`:

- detections present: `e.kind`, `e.n`, plus the best detection's fields
  (same per-kind fields as the table above)
- scene became empty: `e.kind`, `e.n == 0` — sent once per transition
- camera link: `e.kind == "link"`, `e.ok` (1 up / 0 down)
````

- [ ] **Step 3: Add vision stubs to the `## Validation stubs` section**

If the copied file has a `## Validation stubs` section, append the block
below to it; otherwise add the section at the end of the file:

````markdown
## Validation stubs

```lua
-- Vision: pretend a person-detection model sees one person mid-frame.
vision = {
  kind = function() return "boxes" end,
  count = function() return 1 end,
  detection = function(i)
    if i ~= nil and i > 1 then return nil end
    return {x = 80, y = 60, w = 40, h = 80, score = 85, target = 0}
  end,
  keypoint = function(i, k)
    return {x = 96, y = 60, score = 80}
  end,
  age_ms = function() return 120 end,
  ok = function() return true end,
}
```
````

- [ ] **Step 4: Sanity-check the doc**

Per the house rule, DEVICE-SKILL.md is for Lua app authors targeting the
sandbox — re-read and strip anything that talks about C++, drivers, I2C
internals, or firmware builds.

---

### Task 7: Demo apps

**Files:**
- Create: `examples/m5stick-grove-vision-ai/device-apps/presence.lua`
- Create: `examples/m5stick-grove-vision-ai/device-apps/tracker.lua`
- Create: `examples/m5stick-grove-vision-ai/device-apps/skeleton.lua`

Also copy the inherited generic apps so the example is self-contained:

```bash
cp examples/m5stick-demo/device-apps/hello.lua \
   examples/m5stick-demo/device-apps/bounce.lua \
   examples/m5stick-demo/device-apps/buttons-buzzer.lua \
   examples/m5stick-grove-vision-ai/device-apps/
```

- [ ] **Step 1: Write `presence.lua` (event-driven; any detection model)**

```lua
-- presence.lua — beep and flash when the camera sees something.
-- Works with any detection/classification model. Event-driven: no polling.

local state = "waiting"   -- "waiting" | "seen"
local last_target = -1
local last_score = 0

local function draw()
  screen.clear()
  if state == "seen" then
    screen.fill_rect(0, 0, screen.width(), screen.height(), 0, 80, 0)
    screen.text(10, 15, "SEEN!", 4, 255, 255, 255)
    screen.text(10, 70, "target " .. last_target .. "  score " .. last_score, 2)
  else
    screen.text(10, 15, "Watching...", 3, 0, 200, 200)
    if not vision.ok() then
      screen.text(10, 70, "camera offline", 2, 255, 80, 80)
    end
  end
  screen.flip()
end

function init(ctx)
  draw()
end

function on_event(ctx, e)
  if e.name ~= "vision" then return end
  if e.kind == "link" then
    draw()
    return
  end
  if e.n and e.n > 0 then
    if state ~= "seen" then buzzer.beep(880, 80) end
    state = "seen"
    last_target = e.target or -1
    last_score = e.score or 0
  else
    state = "waiting"
  end
  draw()
end
```

- [ ] **Step 2: Write `tracker.lua` (poll-driven; box-emitting models)**

```lua
-- tracker.lua — draw the best detection's box live on the LCD.
-- Works with boxes and pose kinds. Polls vision.* each tick.

-- Model-frame size. SenseCraft detection models typically infer on 192x192;
-- eyeball the serial log (x/y/w/h ranges) and adjust if your model differs.
local FRAME = 192

local function sx(v) return math.floor(v * screen.width() / FRAME) end
local function sy(v) return math.floor(v * screen.height() / FRAME) end

function init(ctx)
  screen.clear()
  screen.text(10, 10, "Tracker", 3)
  screen.flip()
end

function on_tick(ctx, dt)
  screen.clear()
  local kind = vision.kind()
  local d = vision.detection(1)
  if d ~= nil and (kind == "boxes" or kind == "pose") then
    -- box is centered on (x, y) in model frame
    local x = sx(d.x - d.w / 2)
    local y = sy(d.y - d.h / 2)
    screen.rect(x, y, sx(d.w), sy(d.h), 0, 255, 0)
    screen.text(5, 5, "t=" .. d.target .. " s=" .. d.score, 2, 0, 255, 0)
    screen.text(5, screen.height() - 25, vision.count() .. " in frame", 2)
  elseif not vision.ok() then
    screen.text(10, 50, "camera offline", 2, 255, 80, 80)
  else
    screen.text(10, 50, "nothing in frame", 2, 120, 120, 120)
  end
  screen.flip()
end
```

Note for the engineer: whether SSCMA box x/y is the centre or the top-left
corner must be confirmed on the bench (serial log makes it obvious — stand
still, compare). Adjust the `- d.w / 2` terms if it's top-left.

- [ ] **Step 3: Write `skeleton.lua` (pose model)**

```lua
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

local function sx(v) return math.floor(v * screen.width() / FRAME) end
local function sy(v) return math.floor(v * screen.height() / FRAME) end

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
      screen.line(sx(a.x), sy(a.y), sx(b.x), sy(b.y), 0, 255, 255)
    end
  end
  -- head: a dot at the nose
  local nose = vision.keypoint(1, 1)
  if nose and nose.score >= MIN_SCORE then
    screen.fill_rect(sx(nose.x) - 3, sy(nose.y) - 3, 6, 6, 255, 255, 0)
  end
  screen.flip()
end
```

- [ ] **Step 4: Validate all three apps locally**

Use the resident plugin's validator with the new DEVICE-SKILL (its stubs make
`vision.*` resolvable):

Invoke the `resident:validate-app` skill for each of
`device-apps/presence.lua`, `device-apps/tracker.lua`,
`device-apps/skeleton.lua`, passing
`examples/m5stick-grove-vision-ai/DEVICE-SKILL.md` as the reference doc.
Expected: all three pass (compile + init + a few ticks). `skeleton.lua` will
exercise only its "not pose" path under the default boxes stub — that's fine;
its pose path runs on the bench.

---

### Task 8: README

**Files:**
- Create: `examples/m5stick-grove-vision-ai/README.md`

- [ ] **Step 1: Write the README**

```markdown
# m5stick-grove-vision-ai

[Resident](../..) example: an M5StickS3 with a Seeed **Grove Vision AI V2**
camera module on the Grove port. The Vision AI module runs a SenseCraft model
entirely on-device (Himax WiseEye2: Cortex-M55 + Ethos-U55 NPU) and the
M5Stick polls results over I2C, feeding them into the Lua sandbox as events
and a pollable `vision.*` module — so hot-reloadable Lua apps can react to
what the camera sees.

The driver is **model-agnostic**: flash a different SenseCraft model onto the
camera module and the same firmware keeps working; only the result kind
(`boxes` / `classes` / `pose` / `points`) changes. The driver also logs every
frame verbosely to serial so you can eyeball what an unfamiliar model emits.

## Hardware

| Component | Details |
|---|---|
| M5StickS3 | ESP32-S3, 1.14" 135×240 LCD, Grove port (I2C: SDA=GPIO9, SCL=GPIO10) |
| Grove Vision AI Module V2 | Himax WiseEye2 HX6538; SSCMA/I2C host interface (addr 0x62) |
| OV5647 camera | CSI ribbon to the Vision AI module |

Wiring: camera ribbon → Vision AI module, Grove cable → M5StickS3 Grove
port. The Grove port powers the module; USB on the module is only needed
while flashing models.

## Flash a model onto the camera module

1. Open [SenseCraft AI](https://sensecraft.seeed.cc/ai/home) in Chrome/Edge.
2. Connect the Vision AI module via USB-C, pick **Grove Vision AI V2**.
3. Choose a model (person detection, face detection, gesture
   rock-paper-scissors, human pose, …) and deploy.
4. Unplug USB; the module now runs that model standalone.

## Build and flash the M5Stick

```sh
cd device
pio run -e m5sticks3 -t upload
pio device monitor          # watch [vision] logs (115200 baud)
pio test -e native          # vision_frame helper unit tests (no hardware)
```

On boot the device joins WiFi (Courier config portal on first run), connects
to the Resident relay, and shows its device ID. Push an app:

```sh
./send-app.sh --device-id <id-from-screen> device-apps/presence.lua
```

## Demo apps

- `device-apps/presence.lua` — beep + flash when anything is detected
  (any detection model).
- `device-apps/tracker.lua` — draw the best detection's box live (detection
  or pose models).
- `device-apps/skeleton.lua` — stick figure from the 17 pose keypoints
  (human pose model).
- plus the generic m5stick apps (`hello.lua`, `bounce.lua`,
  `buttons-buzzer.lua`).

The Lua surface (screen/imu/buzzer/button/vision) is documented in
[DEVICE-SKILL.md](DEVICE-SKILL.md).
```

---

### Task 9: CI build list + full test sweep

**Files:**
- Modify: `tools/run-tests.py:69-76` (the `PLATFORMIO_EXAMPLES` list)

- [ ] **Step 1: Add the example to the hardcoded build list**

In `tools/run-tests.py`, change:

```python
PLATFORMIO_EXAMPLES = [
    ROOT / "examples" / "m5stick-demo" / "device",
    ROOT / "examples" / "adafruit-esp32-s2-feather" / "device",
```

to:

```python
PLATFORMIO_EXAMPLES = [
    ROOT / "examples" / "m5stick-demo" / "device",
    ROOT / "examples" / "m5stick-grove-vision-ai" / "device",
    ROOT / "examples" / "adafruit-esp32-s2-feather" / "device",
```

Note: `pio run` inside the example builds ALL its envs including `native`;
if the runner builds with `pio run` (not `-e`), the native env builds too —
that's harmless (it compiles nothing without test sources). Check the
runner's invocation; if it errors on the native env, scope it with
`pio run -e m5sticks3` semantics per the runner's existing pattern.

- [ ] **Step 2: Run everything**

```bash
cd /Users/matt/code/resident
./tools/run-tests.py all
cd examples/m5stick-grove-vision-ai/device && pio test -e native
```

Expected: unit tests pass (root 20 + example 6 run separately), cppcheck
clean, all example builds succeed.

---

### Task 10: ⚙️ CHECKPOINT — bench-run the apps, then final commit

- [ ] **Step 1: Hand to Matt**

Push each demo app to the device (`send-app.sh`) with a matching model
flashed and confirm: presence beeps on appearance; tracker's box follows you
(verify centre-vs-corner; fix `tracker.lua` if boxes land offset); skeleton
draws a plausible figure on the pose model. Eyeball serial output per model —
this is the spike's payoff; capture anything surprising in the napkin
(e.g. actual frame size per model, info() label table format).

- [ ] **Step 2: Commit docs + apps + CI (after Matt's confirmation)**

```bash
cd /Users/matt/code/resident
git add examples/m5stick-grove-vision-ai/ tools/run-tests.py
git commit -m "docs(examples): m5stick-grove-vision-ai apps, DEVICE-SKILL, README; CI build

Three demo apps (presence/tracker/skeleton) exercising events, the vision.*
poll API, and all SSCMA result kinds; DEVICE-SKILL.md for Lua app authors
with vision validation stubs; README covering wiring and SenseCraft model
flashing. Example added to run-tests.py's build list.

Verified on hardware with <models Matt tested>.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Self-review notes (already applied)

- Spec coverage: §1 layout → T1; §2 driver → T3; §3 events → T3 (emitFrameEvent); §4 Lua module → T3 (bindings); §5 apps → T7; §6 verbose logging → T3 (logFrame/logModelInfo); §7 testing → T2/T9; target_name() stretch goal → deliberately not planned (spec marks it opportunistic; bench `info()` output decides).
- Bench-unknowns called out where they bite: box centre-vs-corner (T7 tracker), `name()`/`info()` content and perf availability (T5), per-model frame size (T7 FRAME constant).
- Type consistency: `vision::Frame/Box/Point/Classification/Person/Kind` defined once in T2, used in T3; Lua method names in `registerModule` match the implementations; event field names match DEVICE-SKILL's event docs.
```
