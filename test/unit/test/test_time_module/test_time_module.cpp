// The `time` module: Python 3's time, in the sandbox. Its calendar half is
// deprecated for `datetime` (test_datetime_module, which also covers the
// warning) but works unchanged, and this suite holds it to that.
//
// The pure half (calendar maths, strftime) is ResidentTimeCore.h and is
// tested first, against dates whose answers are known independently: the
// epoch, leap days, the 2038 rollover, and what C's strftime prints in the C
// locale. Then the Lua surface, with ezTime's stub supplying "now" and a zone.
#include <unity.h>

#include "ResidentSandbox.cpp"

namespace {

using Resident::timecore::Tm;
using Resident::timecore::breakDown;
using Resident::timecore::format;
using Resident::timecore::wallSeconds;

Resident::Sandbox* sandbox = nullptr;

// 2026-10-03 14:05:09 UTC, a Saturday.
constexpr int64_t kSat = 1791036309;

void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = "function on_tick(ctx, dt) end\n";
  sandbox->injectMessage("test", "app", doc);
}

bool lua(const char* chunk) { return sandbox->loadChunk(chunk); }
bool flag(const char* name) { return sandbox->luaGlobalBoolForTest(name); }
int num(const char* name) { return sandbox->luaGlobalIntForTest(name); }

}  // namespace

void setUp(void) { testMillis() = 0; ezTimeStub::reset(); }
void tearDown(void) { delete sandbox; sandbox = nullptr; }

// ── the pure core ────────────────────────────────────────────────────────

void test_epoch_breaks_down_to_thursday_1970(void) {
  Tm t = breakDown(0, 0, 0, "UTC");
  TEST_ASSERT_EQUAL_INT(1970, t.year);
  TEST_ASSERT_EQUAL_INT(1, t.mon);
  TEST_ASSERT_EQUAL_INT(1, t.mday);
  TEST_ASSERT_EQUAL_INT(3, t.wday);   // Thursday, Monday = 0
  TEST_ASSERT_EQUAL_INT(1, t.yday);
}

void test_known_instant_breaks_down(void) {
  Tm t = breakDown(kSat, 0, 0, "UTC");
  TEST_ASSERT_EQUAL_INT(2026, t.year);
  TEST_ASSERT_EQUAL_INT(10, t.mon);
  TEST_ASSERT_EQUAL_INT(3, t.mday);
  TEST_ASSERT_EQUAL_INT(14, t.hour);
  TEST_ASSERT_EQUAL_INT(5, t.min);
  TEST_ASSERT_EQUAL_INT(9, t.sec);
  TEST_ASSERT_EQUAL_INT(5, t.wday);   // Saturday
  TEST_ASSERT_EQUAL_INT(276, t.yday);
}

void test_leap_day_and_year_end(void) {
  Tm leap = breakDown(951782400, 0, 0, "UTC");   // 2000-02-29 00:00:00
  TEST_ASSERT_EQUAL_INT(2000, leap.year);
  TEST_ASSERT_EQUAL_INT(2, leap.mon);
  TEST_ASSERT_EQUAL_INT(29, leap.mday);
  Tm nye = breakDown(1798761599, 0, 0, "UTC");   // 2026-12-31 23:59:59
  TEST_ASSERT_EQUAL_INT(365, nye.yday);
  TEST_ASSERT_EQUAL_INT(23, nye.hour);
}

void test_past_2038_and_before_1970(void) {
  Tm after = breakDown(2147483648LL, 0, 0, "UTC");   // 2038-01-19 03:14:08
  TEST_ASSERT_EQUAL_INT(2038, after.year);
  TEST_ASSERT_EQUAL_INT(19, after.mday);
  TEST_ASSERT_EQUAL_INT(3, after.hour);
  Tm before = breakDown(-1, 0, 0, "UTC");            // 1969-12-31 23:59:59
  TEST_ASSERT_EQUAL_INT(1969, before.year);
  TEST_ASSERT_EQUAL_INT(31, before.mday);
  TEST_ASSERT_EQUAL_INT(59, before.sec);
  TEST_ASSERT_EQUAL_INT(2, before.wday);             // Wednesday
}

void test_offset_moves_the_wall_clock_not_the_instant(void) {
  Tm bst = breakDown(kSat, 3600, 1, "BST");
  TEST_ASSERT_EQUAL_INT(15, bst.hour);
  TEST_ASSERT_EQUAL_INT(3600, bst.gmtoff);
  TEST_ASSERT_EQUAL_STRING("BST", bst.zone);
  // Across midnight: 23:30 UTC is the next day at UTC+1.
  Tm late = breakDown(kSat - 14 * 3600 - 5 * 60 - 9 + 23 * 3600 + 30 * 60, 3600, 1, "BST");
  TEST_ASSERT_EQUAL_INT(4, late.mday);
  TEST_ASSERT_EQUAL_INT(6, late.wday);   // Sunday
}

void test_wall_seconds_inverts_breakdown_and_normalises(void) {
  Tm t = breakDown(kSat, 0, 0, "UTC");
  TEST_ASSERT_TRUE(wallSeconds(t) == kSat);
  Tm over;  // month 13, day 0 -> 31 Dec of that year
  over.year = 2026; over.mon = 13; over.mday = 0;
  Tm back = breakDown(wallSeconds(over), 0, 0, "UTC");
  TEST_ASSERT_EQUAL_INT(2026, back.year);
  TEST_ASSERT_EQUAL_INT(12, back.mon);
  TEST_ASSERT_EQUAL_INT(31, back.mday);
}

void test_strftime_matches_the_c_locale(void) {
  Tm t = breakDown(kSat, 3600, 1, "BST");
  char buf[128];
  format(buf, sizeof(buf), "%a %A %b %B %d %e %H %I %j %m %M %p %S %w %y %Y %Z %z %%", t);
  TEST_ASSERT_EQUAL_STRING("Sat Saturday Oct October 03  3 15 03 276 10 05 PM 09 6 26 2026 BST +0100 %", buf);
  format(buf, sizeof(buf), "%c|%x|%X", t);
  TEST_ASSERT_EQUAL_STRING("Sat Oct  3 15:05:09 2026|10/03/26|15:05:09", buf);
  // Week numbers: 2026-10-03 is in week 39 counting from Sunday and Monday.
  format(buf, sizeof(buf), "%U %W", t);
  TEST_ASSERT_EQUAL_STRING("39 39", buf);
  // Unknown directives pass through; negative offsets sign correctly.
  Tm ny = breakDown(kSat, -4 * 3600, 1, "EDT");
  format(buf, sizeof(buf), "%Q %z", ny);
  TEST_ASSERT_EQUAL_STRING("%Q -0400", buf);
}

void test_strftime_truncates_like_snprintf(void) {
  Tm t = breakDown(kSat, 0, 0, "UTC");
  char buf[5];
  size_t n = format(buf, sizeof(buf), "%A", t);
  TEST_ASSERT_EQUAL_UINT(8, n);
  TEST_ASSERT_EQUAL_STRING("Satu", buf);
}

// ── the Lua module ───────────────────────────────────────────────────────

void test_module_is_python_shaped_and_old_calls_are_gone(void) {
  build();
  TEST_ASSERT_TRUE(lua(
      "ok = type(time.time) == 'function' and type(time.ticks_ms) == 'function'\n"
      "  and type(time.ticks_diff) == 'function' and time.monotonic == nil\n"
      "  and type(time.gmtime) == 'function' and type(time.localtime) == 'function'\n"
      "  and type(time.mktime) == 'function' and type(time.strftime) == 'function'\n"
      "  and type(time.synced) == 'function'\n"
      "gone = time.hour == nil and time.minute == nil and time.second == nil\n"
      "  and time.is_valid == nil and time.day_id == nil and time.has_timezone == nil\n"));
  TEST_ASSERT_TRUE(flag("ok"));
  TEST_ASSERT_TRUE(flag("gone"));
}

void test_unsynced_clock_reads_the_epoch(void) {
  build();
  TEST_ASSERT_TRUE(lua("s = time.synced(); y = time.localtime().tm_year\n"));
  TEST_ASSERT_FALSE(flag("s"));
  TEST_ASSERT_EQUAL_INT(1970, num("y"));
}

void test_synced_time_and_gmtime(void) {
  ezTimeStub::state().synced = true;
  ezTimeStub::state().epoch = kSat;
  build();
  TEST_ASSERT_TRUE(lua(
      "s = time.synced()\n"
      "whole_ok = math.type(time.time()) == 'integer' and time.time() == 1791036309\n"
      "local g = time.gmtime()\n"
      "fields_ok = g.tm_year == 2026 and g.tm_mon == 10 and g.tm_mday == 3\n"
      "  and g.tm_hour == 14 and g.tm_min == 5 and g.tm_sec == 9\n"
      "  and g.tm_wday == 5 and g.tm_yday == 276 and g.tm_isdst == 0\n"
      "  and g.tm_zone == 'UTC' and g.tm_gmtoff == 0\n"
      "local e = time.gmtime(0)\n"
      "epoch_ok = e.tm_year == 1970 and e.tm_wday == 3\n"));
  TEST_ASSERT_TRUE(flag("s"));
  TEST_ASSERT_TRUE(flag("whole_ok"));
  TEST_ASSERT_TRUE(flag("fields_ok"));
  TEST_ASSERT_TRUE(flag("epoch_ok"));
}

void test_localtime_is_utc_until_a_zone_is_known(void) {
  ezTimeStub::state().synced = true;
  ezTimeStub::state().epoch = kSat;
  build();
  TEST_ASSERT_TRUE(lua("local l = time.localtime(); h = l.tm_hour; utc = l.tm_zone == 'UTC'\n"));
  TEST_ASSERT_EQUAL_INT(14, num("h"));
  TEST_ASSERT_TRUE(flag("utc"));
}

void test_localtime_mktime_strftime_in_a_zone(void) {
  ezTimeStub::State& s = ezTimeStub::state();
  s.synced = true;
  s.epoch = kSat;
  s.zoneAccepted = true;
  s.zoneName = "BST";
  s.offsetMinutesWest = -60;   // UTC+1
  s.dst = true;
  build();
  sandbox->setTimezone("Europe/London");
  TEST_ASSERT_TRUE(sandbox->hasTimezone());
  TEST_ASSERT_TRUE(lua(
      "local l = time.localtime()\n"
      "h = l.tm_hour\n"
      "zone_ok = l.tm_zone == 'BST' and l.tm_gmtoff == 3600 and l.tm_isdst == 1\n"
      "round_trip = math.type(time.mktime(l)) == 'integer' and time.mktime(l) == 1791036309\n"
      "later = time.localtime(time.time() + 60).tm_min == 6\n"
      "label_ok = time.strftime('%A %H:%M %Z') == 'Saturday 15:05 BST'\n"
      "built_ok = time.strftime('%a %d %b', { tm_year = 2026, tm_mon = 12, tm_mday = 25 }) == 'Fri 25 Dec'\n"
      "carry_ok = time.mktime({ tm_year = 2026, tm_mon = 10, tm_mday = 3, tm_hour = 24 })\n"
      "  == time.mktime({ tm_year = 2026, tm_mon = 10, tm_mday = 4 })\n"));
  TEST_ASSERT_EQUAL_INT(15, num("h"));
  TEST_ASSERT_TRUE(flag("zone_ok"));
  TEST_ASSERT_TRUE(flag("round_trip"));
  TEST_ASSERT_TRUE(flag("later"));
  TEST_ASSERT_TRUE(flag("label_ok"));
  TEST_ASSERT_TRUE(flag("built_ok"));
  TEST_ASSERT_TRUE(flag("carry_ok"));
}

void test_mktime_rejects_a_table_without_a_date(void) {
  build();
  TEST_ASSERT_TRUE(lua("ok, err = pcall(time.mktime, { tm_hour = 3 })\nfailed = not ok\n"));
  TEST_ASSERT_TRUE(flag("failed"));
}

void test_ticks_follow_millis_and_diff_across_the_wrap(void) {
  build();
  testMillis() = 1500;
  TEST_ASSERT_TRUE(lua("a = time.ticks_ms()\n"));
  testMillis() = 4000;
  TEST_ASSERT_TRUE(lua("b = time.ticks_ms()\nd = time.ticks_diff(b, a)\nback = time.ticks_diff(a, b)\n"));
  TEST_ASSERT_EQUAL_INT(2500, num("d"));
  TEST_ASSERT_EQUAL_INT(-2500, num("back"));
  // 2^32 - 100 ms, then 50 ms past the wrap: the counter goes negative and
  // round again, the difference does not.
  testMillis() = 4294967196UL;
  TEST_ASSERT_TRUE(lua("w0 = time.ticks_ms()\n"));
  testMillis() = 50;
  TEST_ASSERT_TRUE(lua("w1 = time.ticks_ms()\nwd = time.ticks_diff(w1, w0)\n"));
  TEST_ASSERT_EQUAL_INT(150, num("wd"));
}

void test_shader_globals_are_gone(void) {
  build();
  TEST_ASSERT_TRUE(lua(
      "gone = rgb == nil and fract == nil and beat == nil and noise2d == nil\n"
      "  and floor == nil and sin == nil and fmod == nil and min == nil\n"
      "math_ok = math.floor(2.5) == 2\n"));
  TEST_ASSERT_TRUE(flag("gone"));
  TEST_ASSERT_TRUE(flag("math_ok"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_epoch_breaks_down_to_thursday_1970);
  RUN_TEST(test_known_instant_breaks_down);
  RUN_TEST(test_leap_day_and_year_end);
  RUN_TEST(test_past_2038_and_before_1970);
  RUN_TEST(test_offset_moves_the_wall_clock_not_the_instant);
  RUN_TEST(test_wall_seconds_inverts_breakdown_and_normalises);
  RUN_TEST(test_strftime_matches_the_c_locale);
  RUN_TEST(test_strftime_truncates_like_snprintf);
  RUN_TEST(test_module_is_python_shaped_and_old_calls_are_gone);
  RUN_TEST(test_unsynced_clock_reads_the_epoch);
  RUN_TEST(test_synced_time_and_gmtime);
  RUN_TEST(test_localtime_is_utc_until_a_zone_is_known);
  RUN_TEST(test_localtime_mktime_strftime_in_a_zone);
  RUN_TEST(test_mktime_rejects_a_table_without_a_date);
  RUN_TEST(test_ticks_follow_millis_and_diff_across_the_wrap);
  RUN_TEST(test_shader_globals_are_gone);
  return UNITY_END();
}
