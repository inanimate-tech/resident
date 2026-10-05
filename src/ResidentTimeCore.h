#ifndef RESIDENT_TIME_CORE_H
#define RESIDENT_TIME_CORE_H

// The pure half of the Lua `time` module: calendar arithmetic and strftime,
// with no clock, no timezone database and no Lua. Sandbox supplies "now" and
// the zone (ezTime); everything here is plain integer maths, so the device,
// the browser sim and the native tests agree to the byte.
//
// The module is modelled on Python 3's `time`, and so is this struct: the
// field names and ranges are Python's struct_time, not C's struct tm and not
// Lua's os.date("*t"). In particular tm_mon is 1..12, tm_wday is 0..6 with
// MONDAY = 0, and tm_yday is 1..366.
//
// Calendar conversions are Howard Hinnant's days_from_civil/civil_from_days:
// proleptic Gregorian, exact over the whole int64 range we could care about,
// and independent of the C library's gmtime (which newlib, glibc and
// emscripten each implement, and not identically at the edges).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Resident {
namespace timecore {

struct Tm {
  int year = 1970;
  int mon = 1;      // 1..12
  int mday = 1;     // 1..31
  int hour = 0;     // 0..23
  int min = 0;      // 0..59
  int sec = 0;      // 0..59
  int wday = 3;     // 0..6, Monday = 0 (1970-01-01 was a Thursday)
  int yday = 1;     // 1..366
  int isdst = 0;    // 1, 0, or -1 = unknown (mktime input only)
  int32_t gmtoff = 0;     // seconds EAST of UTC
  char zone[16] = "UTC";  // abbreviation, e.g. "BST"
  bool hasZone = true;    // false for a bare date: %z and %Z print nothing
};

// Days since 1970-01-01 for a civil date (m 1..12, d 1..31).
inline int64_t daysFromCivil(int64_t y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;                                  // [0, 399]
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
  return era * 146097 + doe - 719468;
}

// The civil date for days since 1970-01-01.
inline void civilFromDays(int64_t z, int64_t& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y += m <= 2;
}

inline bool isLeap(int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

// Floor division, for negative seconds (dates before 1970).
inline int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

// Weekday (Monday = 0) and day of year (1..366) for a civil date.
inline void weekdayAndYearday(int64_t y, int m, int d, int& wday, int& yday) {
  const int64_t days = daysFromCivil(y, m, d);
  // 1970-01-01 is a Thursday = 3 when Monday = 0.
  wday = (int)(((days + 3) % 7 + 7) % 7);
  yday = (int)(days - daysFromCivil(y, 1, 1) + 1);
}

// Break @p utcSeconds into fields, displaced by @p gmtoff seconds east of
// UTC. The caller supplies the zone's offset, DST flag and abbreviation for
// that instant; gmtime is breakDown(t, 0, 0, "UTC").
inline Tm breakDown(int64_t utcSeconds, int32_t gmtoff, int isdst, const char* zone) {
  Tm t;
  const int64_t local = utcSeconds + gmtoff;
  const int64_t days = floorDiv(local, 86400);
  const int64_t secs = local - days * 86400;
  int64_t y; int m, d;
  civilFromDays(days, y, m, d);
  t.year = (int)y; t.mon = m; t.mday = d;
  t.hour = (int)(secs / 3600);
  t.min = (int)(secs / 60 % 60);
  t.sec = (int)(secs % 60);
  weekdayAndYearday(y, m, d, t.wday, t.yday);
  t.isdst = isdst;
  t.gmtoff = gmtoff;
  std::snprintf(t.zone, sizeof(t.zone), "%s", zone ? zone : "");
  return t;
}

// The fields read as a wall-clock time, in seconds since the epoch AS IF that
// wall clock were UTC. Out-of-range fields carry, as C's mktime normalises
// them: month 13 is January of the next year, day 0 the last of the previous
// month, minute 90 an hour and a half. Weekday, yearday, zone and DST are
// ignored (mktime recomputes them).
inline int64_t wallSeconds(const Tm& t) {
  int64_t y = t.year;
  int64_t mon0 = (int64_t)t.mon - 1;
  y += floorDiv(mon0, 12);
  mon0 -= floorDiv(mon0, 12) * 12;
  const int64_t days = daysFromCivil(y, (int)mon0 + 1, 1) + (int64_t)t.mday - 1;
  return days * 86400 + (int64_t)t.hour * 3600 + (int64_t)t.min * 60 + (int64_t)t.sec;
}

namespace detail {
static const char* const kDays[7] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                     "Friday", "Saturday", "Sunday"};
static const char* const kMonths[12] = {"January", "February", "March", "April", "May", "June",
                                        "July", "August", "September", "October",
                                        "November", "December"};

struct Out {
  char* buf; size_t cap; size_t len;
  void put(char c) { if (len + 1 < cap) buf[len] = c; ++len; }
  void puts(const char* s) { while (*s) put(*s++); }
  void putn(const char* s, size_t n) { for (size_t i = 0; i < n && s[i]; ++i) put(s[i]); }
  void num(long v, int width, char pad) {
    char tmp[24];
    std::snprintf(tmp, sizeof(tmp), pad == '0' ? "%0*ld" : "%*ld", width, v);
    puts(tmp);
  }
};

// Week of the year with weeks starting on @p firstDay (0 = Monday, 6 =
// Sunday): days before the year's first such day are in week 0 (C's %U/%W).
inline int weekNumber(const Tm& t, int firstDay) {
  const int wdayFromFirst = ((t.wday - firstDay) % 7 + 7) % 7;
  return (t.yday - 1 + 7 - wdayFromFirst) / 7;
}
}  // namespace detail

// strftime in the C locale, the directive set Python 3 documents as
// portable: %a %A %b %B %c %d %H %I %j %m %M %p %S %U %w %W %x %X %y %Y %Z
// %z %%, plus %e (space-padded day, which %c uses). %w is C's: Sunday = 0.
// A Tm with no zone (hasZone false, a date) prints %z and %Z as nothing, as
// Python does for a naive value.
// An unknown directive is copied through as written. Implemented here rather
// than by the C library's strftime because its %Z reads the process's TZ,
// not the zone this sandbox resolved, and because the libraries disagree on
// what an unknown directive does.
//
// Writes at most cap-1 bytes and a NUL; returns the length the full output
// needed (so a return >= cap means it was truncated), like snprintf.
inline size_t format(char* buf, size_t cap, const char* fmt, const Tm& t) {
  detail::Out o{buf, cap, 0};
  const int wday = ((t.wday % 7) + 7) % 7;
  const int mon0 = ((t.mon - 1) % 12 + 12) % 12;
  for (const char* p = fmt ? fmt : ""; *p; ++p) {
    if (*p != '%') { o.put(*p); continue; }
    const char c = *++p;
    if (!c) { o.put('%'); break; }
    switch (c) {
      case 'a': o.putn(detail::kDays[wday], 3); break;
      case 'A': o.puts(detail::kDays[wday]); break;
      case 'b': o.putn(detail::kMonths[mon0], 3); break;
      case 'B': o.puts(detail::kMonths[mon0]); break;
      case 'c': {  // C locale: "Sat Oct  3 14:05:09 2026"
        char tmp[64];
        format(tmp, sizeof(tmp), "%a %b %e %H:%M:%S %Y", t);
        o.puts(tmp);
        break;
      }
      case 'd': o.num(t.mday, 2, '0'); break;
      case 'e': o.num(t.mday, 2, ' '); break;
      case 'H': o.num(t.hour, 2, '0'); break;
      case 'I': o.num(t.hour % 12 == 0 ? 12 : t.hour % 12, 2, '0'); break;
      case 'j': o.num(t.yday, 3, '0'); break;
      case 'm': o.num(t.mon, 2, '0'); break;
      case 'M': o.num(t.min, 2, '0'); break;
      case 'p': o.puts(t.hour < 12 ? "AM" : "PM"); break;
      case 'S': o.num(t.sec, 2, '0'); break;
      case 'U': o.num(detail::weekNumber(t, 6), 2, '0'); break;
      case 'w': o.num((wday + 1) % 7, 1, '0'); break;
      case 'W': o.num(detail::weekNumber(t, 0), 2, '0'); break;
      case 'x': {  // C locale: "10/03/26"
        char tmp[32];
        format(tmp, sizeof(tmp), "%m/%d/%y", t);
        o.puts(tmp);
        break;
      }
      case 'X': {
        char tmp[32];
        format(tmp, sizeof(tmp), "%H:%M:%S", t);
        o.puts(tmp);
        break;
      }
      case 'y': o.num(((t.year % 100) + 100) % 100, 2, '0'); break;
      case 'Y': o.num(t.year, 1, '0'); break;
      case 'Z': if (t.hasZone) o.puts(t.zone); break;
      case 'z': {
        if (!t.hasZone) break;
        const int32_t off = t.gmtoff;
        const int32_t a = off < 0 ? -off : off;
        o.put(off < 0 ? '-' : '+');
        o.num(a / 3600, 2, '0');
        o.num(a / 60 % 60, 2, '0');
        break;
      }
      case '%': o.put('%'); break;
      default: o.put('%'); o.put(c); break;
    }
  }
  if (cap) buf[o.len < cap ? o.len : cap - 1] = '\0';
  return o.len;
}

}  // namespace timecore
}  // namespace Resident

#endif  // RESIDENT_TIME_CORE_H
