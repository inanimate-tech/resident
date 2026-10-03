// test/unit/include/ezTime.h
//
// Minimal ezTime stub for native unit tests.
//
// The real <ezTime.h> needs Arduino networking (UDP lookups to
// timezoned.rop.nl) and doesn't compile under the native PlatformIO env.
// This carries only what Resident::Sandbox touches: the UTC clock (now, ms),
// the zone conversion (tzTime) and setLocation.
//
// By default the clock is unset and setLocation() declines, which matches a
// device before its first NTP exchange and zone lookup. A test that needs a
// clock or a zone sets ezTimeStub::state(): the epoch "now", and optionally a
// fixed-offset zone (no DST rules — a test that needs a DST edge supplies the
// offset for each side itself).
#pragma once
#include <Arduino.h>
#include <cstdint>
#include <ctime>

typedef enum { LOCAL_TIME, UTC_TIME } ezLocalOrUTC_t;

namespace ezTimeStub {
struct State {
  bool synced = false;
  int64_t epoch = 0;          // UTC.now()
  uint16_t ms = 0;            // UTC.ms()
  bool zoneAccepted = false;  // setLocation() succeeds
  const char* zoneName = "UTC";
  int16_t offsetMinutesWest = 0;  // ezTime's sign: minutes WEST of UTC
  bool dst = false;
};
inline State& state() { static State s; return s; }
inline void reset() { state() = State(); }
}  // namespace ezTimeStub

class Timezone {
public:
    bool setLocation(const String& ianaZone) { (void)ianaZone; return ezTimeStub::state().zoneAccepted; }
    time_t now() { return (time_t)ezTimeStub::state().epoch; }
    uint16_t ms() { return ezTimeStub::state().ms; }
    // UTC -> local (with the zone's name, DST flag and offset), as ezTime does.
    time_t tzTime(time_t t, ezLocalOrUTC_t local_or_utc, String& tzname, bool& is_dst,
                  int16_t& offset) {
        const ezTimeStub::State& s = ezTimeStub::state();
        tzname = s.zoneName;
        is_dst = s.dst;
        offset = s.offsetMinutesWest;
        return local_or_utc == UTC_TIME ? t - offset * 60LL : t + offset * 60LL;
    }
    time_t tzTime(time_t t, ezLocalOrUTC_t local_or_utc) {
        String n; bool d; int16_t o;
        return tzTime(t, local_or_utc, n, d, o);
    }
};

// ezTime's built-in UTC instance. Inline (C++17) so all TUs share one.
inline Timezone UTC;

enum timeStatus_t { timeNotSet, timeNeedsSync, timeSet };
inline timeStatus_t timeStatus() { return ezTimeStub::state().synced ? timeSet : timeNotSet; }
