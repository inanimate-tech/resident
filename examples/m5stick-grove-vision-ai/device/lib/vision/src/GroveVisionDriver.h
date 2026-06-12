#ifndef GROVE_VISION_DRIVER_H
#define GROVE_VISION_DRIVER_H

#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <Seeed_Arduino_SSCMA.h>
#include "vision_frame.h"

static constexpr uint8_t GROVE_VISION_I2C_ADDR = 0x62;  // SSCMA default

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
  bool probeModule();

  GroveVisionConfig _config;
  SSCMA _ai;
  vision::Frame _frame;

  bool _linkUp = false;
  bool _beginOk = false;
  uint8_t _failStreak = 0;
  unsigned long _lastPollMs = 0;
  unsigned long _lastFrameMs = 0;   // last successful invoke
  int _lastCount = 0;               // for the transition-to-empty event
  vision::Kind _lastKind = vision::Kind::None;

  static constexpr uint8_t FAILS_TO_LINK_DOWN = 3;
  static constexpr uint32_t RETRY_WHEN_DOWN_MS = 2000;
};

#endif  // GROVE_VISION_DRIVER_H
