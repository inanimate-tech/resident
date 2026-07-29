// src/ResidentEvents.h — the data-plane emitter. An internal Extension that
// registers the Lua `events` module (events.send) and owns the token-bucket
// rate limiter. The envelope itself is built by Sandbox::publishEvent so the
// C++ path (wrapper aliases) and the Lua path share one implementation.
//
// Linkage note: this library ships as a single translation unit
// (src/ResidentSandbox.cpp — the only .cpp in src/) and the native unit
// tests compile suites that `#include "ResidentSandbox.cpp"` directly, with
// nothing else in the link. A second ResidentEvents.cpp would never be
// compiled by the test harness (it has no src_filter that would pull it in),
// so every method that doesn't need Sandbox's complete type is defined
// inline right here. The one method that does need it — send(), which calls
// back into Sandbox::publishEvent() — is instead defined out-of-line in
// ResidentSandbox.cpp, right after Sandbox::publishEvent, where Sandbox is
// a complete type. That keeps this class buildable from a single header
// while still compiling for both the native test suites (which pull in
// ResidentSandbox.cpp as one TU) and the PlatformIO library build (which
// compiles src/ResidentSandbox.cpp as the library's sole source file).
#ifndef RESIDENT_EVENTS_H
#define RESIDENT_EVENTS_H

#include "ResidentExtension.h"
#include "ResidentLuaModule.h"

extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}

namespace Resident {

class Sandbox;  // forward decl — send()'s body lives in ResidentSandbox.cpp

class EventsModule : public Extension {
public:
  void bind(Sandbox* sandbox) { _sandbox = sandbox; }

  const char* name() const override { return "events"; }

  void registerModule(LuaModule& m) override {
    m.method<EventsModule, &EventsModule::send>("send");
  }

  void onAppReset() override {
    _tokens = MAX_TOKENS;
    _lastRefillMillis = 0;
  }

  // Token bucket: 5 events/s sustained, burst of 10 (x1000 fixed-point).
  // Called from Sandbox::publishEvent (not from send()) so the Lua path
  // (events.send) and the C++ path (publishEvent callers, e.g. wrapper
  // aliases) share the same limiter.
  bool takeToken() {
    refillTokens();
    if (_tokens < 1000) return false;
    _tokens -= 1000;
    return true;
  }

private:
  Sandbox* _sandbox = nullptr;
  int _tokens = 10000;
  static constexpr int MAX_TOKENS = 10000;
  unsigned long _lastRefillMillis = 0;

  void refillTokens() {
    unsigned long now = millis();
    unsigned long elapsed = now - _lastRefillMillis;
    if (elapsed > 0) {
      // Refill: 5 tokens per second = 5000 per 1000ms (x1000 fixed-point).
      unsigned long refill = elapsed * 5;
      _tokens += refill;
      if (_tokens > MAX_TOKENS) {
        _tokens = MAX_TOKENS;
      }
      _lastRefillMillis = now;
    }
  }

  // events.send(name [, data_table]) -> boolean. Ported verbatim (flat-table
  // JSON serializer + rate-limit-via-publishEvent shape) from RoomModule's
  // announce(); defined in ResidentSandbox.cpp — see the linkage note above.
  int send(lua_State* L);
};

} // namespace Resident

#endif // RESIDENT_EVENTS_H
