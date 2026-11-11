// src/ResidentAppHealth.h
//
// App quarantine: the pure half. A persisted app that crashes the device
// would otherwise be restored, and crash it again, on every boot. The sandbox
// keeps a small record in memory that survives a reset but not a power cycle
// (RTC no-init memory on ESP32): armed while an app runs, counting the resets
// that happened while it was armed and were not deliberate. The count belongs
// to the app that crashed, not to whichever app runs next: restoring a saved
// app between a crashing app's attempts neither resets nor inherits its
// count. After `limit` crashes of one app with no other app crashing in
// between, its source hash is quarantined and it is not run again until
// something clears it.
//
// Everything here is plain data and arithmetic, so it is native-tested
// (test/unit/test/test_app_health). Where the record lives and how the reset
// reason is read are the sandbox's (ResidentSandbox.cpp), because they are
// the platform's.
#ifndef RESIDENT_APP_HEALTH_H
#define RESIDENT_APP_HEALTH_H

#include <cstddef>
#include <cstdint>

namespace Resident {
namespace app_health {

// "RAPH". Memory that survives a reset holds whatever it held at power-on
// until something writes it, so a record counts only when its magic and its
// check word both agree.
constexpr uint32_t kMagic = 0x52415048u;

struct Record {
  uint32_t magic;
  uint32_t count;      // crashes of crashHash, with no other app's between
  uint32_t armed;      // nonzero while an app is running
  uint32_t hash;       // source hash of the app that was armed (0 = none)
  uint32_t crashHash;  // the app `count` belongs to (0 = none)
  uint32_t check;      // ~(magic ^ count ^ armed ^ hash ^ crashHash)
};

// What the last reset was, as far as the quarantine cares.
enum class ResetKind {
  PowerOn,     // power-on or unknown: the record means nothing
  Deliberate,  // a restart someone asked for (software, external pin, host)
  Abnormal,    // panic, watchdog, brownout: a crash, if an app was armed
};

struct Decision {
  Record record;     // what to write back
  bool tripped;      // the armed app reached the limit: quarantine it
  uint32_t hash;     // the app to quarantine (when tripped)
  uint32_t crashes;  // how many consecutive crashes it took (when tripped)
};

// FNV-1a over the source. Never 0, which means "no app".
inline uint32_t sourceHash(const char* src, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= static_cast<uint8_t>(src[i]);
    h *= 16777619u;
  }
  return h ? h : 1u;
}

inline uint32_t checkWord(const Record& r) {
  return ~(r.magic ^ r.count ^ r.armed ^ r.hash ^ r.crashHash);
}

inline bool valid(const Record& r) {
  return r.magic == kMagic && r.check == checkWord(r);
}

inline void seal(Record& r) {
  r.magic = kMagic;
  r.check = checkWord(r);
}

inline Record fresh() {
  Record r{};
  seal(r);
  return r;
}

// Boot: read the record the last run left behind. Always returns a record
// that is disarmed (no app is running yet). `limit` 0 disables tripping.
inline Decision decide(const Record& last, ResetKind kind, uint32_t limit) {
  Decision d{fresh(), false, 0, 0};
  if (kind == ResetKind::PowerOn || !valid(last)) return d;

  d.record.count = last.count;
  d.record.hash = last.hash;
  d.record.crashHash = last.crashHash;
  if (last.armed && last.hash && kind == ResetKind::Abnormal) {
    d.record.count = (last.crashHash == last.hash) ? last.count + 1 : 1;
    d.record.crashHash = last.hash;
    if (limit && d.record.count >= limit) {
      d.tripped = true;
      d.hash = last.hash;
      d.crashes = d.record.count;
      d.record.count = 0;
      d.record.crashHash = 0;
    }
  }
  seal(d.record);
  return d;
}

// An app is about to run. The count is left alone: it belongs to the app
// that crashed, and only that app crashing again adds to it.
inline void arm(Record& r, uint32_t hash) {
  if (!valid(r)) r = fresh();
  r.hash = hash;
  r.armed = 1;
  seal(r);
}

// No app is running.
inline void disarm(Record& r) {
  if (!valid(r)) r = fresh();
  r.armed = 0;
  seal(r);
}

// The armed app has run cleanly for long enough: its crashes so far are
// forgiven. Another app's count is not this app's to forgive.
inline void forgive(Record& r) {
  if (!valid(r)) r = fresh();
  if (r.crashHash == r.hash) {
    r.count = 0;
    r.crashHash = 0;
  }
  seal(r);
}

// forget: no app and no history.
inline void reset(Record& r) {
  const uint32_t armed = valid(r) ? r.armed : 0;
  const uint32_t hash = valid(r) ? r.hash : 0;
  r = fresh();
  r.armed = armed;
  r.hash = hash;
  seal(r);
}

}  // namespace app_health
}  // namespace Resident

#endif  // RESIDENT_APP_HEALTH_H
