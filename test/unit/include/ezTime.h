// test/unit/include/ezTime.h
//
// Minimal ezTime stub for native unit tests.
//
// The real <ezTime.h> needs Arduino networking (UDP lookups to
// timezoned.rop.nl) and doesn't compile under the native PlatformIO env.
// Resident::Sandbox only touches Timezone::setLocation/hour/minute/second
// and the UTC global, so that's all that exists here. setLocation() returns
// false: native tests run timezone-less (hasTimezone() stays false), which
// matches a device before its first successful zone lookup.
#pragma once
#include <Arduino.h>

class Timezone {
public:
    bool setLocation(const String& ianaZone) { (void)ianaZone; return false; }
    int hour() { return 0; }
    int minute() { return 0; }
    int second() { return 0; }
};

// ezTime's built-in UTC instance. Inline (C++17) so all TUs share one.
inline Timezone UTC;

// Sync status — native tests run with time never synced (matches a device
// before its first NTP exchange).
enum timeStatus_t { timeNotSet, timeNeedsSync, timeSet };
inline timeStatus_t timeStatus() { return timeNotSet; }
