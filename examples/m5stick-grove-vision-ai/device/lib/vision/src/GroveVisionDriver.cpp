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

// Cheap I2C presence check (~µs) before the expensive SSCMA calls, which
// block for their full timeout (~1s) when the module is absent. Keeps an
// unplugged camera from stalling the sandbox loop on every retry.
bool GroveVisionDriver::probeModule() {
  Wire.beginTransmission(GROVE_VISION_I2C_ADDR);
  return Wire.endTransmission() == 0;
}

void GroveVisionDriver::update() {
  unsigned long now = millis();
  uint32_t interval = _linkUp ? _config.pollMs : RETRY_WHEN_DOWN_MS;
  if (now - _lastPollMs < interval) return;
  _lastPollMs = now;

  if (!_linkUp && !probeModule()) return;  // absent: skip the blocking calls

  if (!_beginOk) {
    // begin() failed entirely (module absent at boot) — retry the handshake.
    _beginOk = _ai.begin(&Wire);
    if (!_beginOk) return;
  }

  pollModule();
}

void GroveVisionDriver::pollModule() {
  if (_ai.invoke(1, false, false) != CMD_OK) {
    if (_linkUp) {
      if (++_failStreak >= FAILS_TO_LINK_DOWN) {
        setLink(false);
      } else if (_config.verboseLog) {
        Serial.printf("[vision] invoke failed (%d/%d)\n",
                      _failStreak, FAILS_TO_LINK_DOWN);
      }
    } else if (_config.verboseLog) {
      Serial.println("[vision] module offline, retrying");
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
  if (_lastCount > 0) _lastKind = _frame.kind;
}

void GroveVisionDriver::copyResults() {
  _frame.kind = vision::Kind::None;
  _frame.boxes.clear();
  _frame.points.clear();
  _frame.classes.clear();
  _frame.people.clear();

  for (const auto& b : _ai.boxes()) {
    _frame.boxes.push_back({b.x, b.y, b.w, b.h, b.score, b.target});
  }
  for (const auto& c : _ai.classes()) {
    _frame.classes.push_back({c.target, c.score});
  }
  // z: upstream SSCMA never parses it — indeterminate memory, so force 0.
  for (const auto& p : _ai.points()) {
    _frame.points.push_back({p.x, p.y, 0, p.score, p.target});
  }
  for (const auto& k : _ai.keypoints()) {
    vision::Person person;
    person.box = {k.box.x, k.box.y, k.box.w, k.box.h, k.box.score, k.box.target};
    // z: upstream SSCMA never parses it — indeterminate memory, so force 0.
    for (const auto& p : k.points) {
      person.points.push_back({p.x, p.y, 0, p.score, p.target});
    }
    _frame.people.push_back(std::move(person));
  }

  _frame.kind = vision::classify(_frame.people.size(), _frame.boxes.size(),
                                 _frame.points.size(), _frame.classes.size());

  // SSCMA only clears a result vector when its key appears in the response,
  // so results from a previously flashed model would otherwise stick around
  // and win the kind precedence forever. Drain after copying.
  _ai.boxes().clear();
  _ai.classes().clear();
  _ai.points().clear();
  _ai.keypoints().clear();
}

void GroveVisionDriver::emitFrameEvent() {
  int n = _frame.count();
  if (n == 0) {
    if (_lastCount > 0) {  // transition to empty, once
      Resident::EventField f[2];
      setStr(f[0], "kind", vision::kindName(_lastKind));  // the kind that just emptied, not "none"
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
