// The `datetime` module: Python's datetime, in the sandbox — and the
// deprecation of `time`'s calendar half that it replaces.
//
// Every expected value below was printed by CPython 3 (datetime, with
// zoneinfo's Europe/London for the DST cases) and pasted in. The zone is
// ezTime's stub: London's 2026 window (BST from 2026-03-29 01:00 UTC to
// 2026-10-25 01:00 UTC), which resolves wall times as ezTime does.
//
// The checks run in Lua: `is(got, want, what)` counts a mismatch and logs it,
// so a failure says which value and what it was.
#include <unity.h>

#include "ResidentSandbox.cpp"

namespace {

Resident::Sandbox* sandbox = nullptr;

// 2026-10-05 12:05:09 UTC, a Monday; 13:05:09 BST.
constexpr int64_t kNow = 1791201909;

const char* kChecks =
    "nfail = 0\n"
    "function is(got, want, what)\n"
    "  if got ~= want then\n"
    "    nfail = nfail + 1\n"
    "    log.info(('MISMATCH %s: got [%s] want [%s]'):format(what, tostring(got), tostring(want)))\n"
    "  end\n"
    "end\n"
    // The message of the error fn(...) raises, without its position, or
    // 'no error'.
    "function err(fn, ...)\n"
    "  local ok, e = pcall(fn, ...)\n"
    "  return ok and 'no error' or (tostring(e):gsub('^%[string \".-\"%]:%d+: ', ''))\n"
    "end\n";

void load(const char* app = "function on_tick(ctx, dt) end\n") {
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = app;
  sandbox->injectMessage("test", "app", doc);
}

void build(bool london = true) {
  ezTimeStub::State& s = ezTimeStub::state();
  s.synced = true;
  s.epoch = kNow;
  if (london) {
    s.zoneAccepted = true;
    s.zoneName = "GMT";
    s.offsetMinutesWest = 0;
    s.dstWindow = true;
    s.dstStart = 1774746000;   // 2026-03-29 01:00 UTC
    s.dstEnd = 1792890000;     // 2026-10-25 01:00 UTC
    s.dstName = "BST";
    s.dstOffsetMinutesWest = -60;
  }
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  if (london) sandbox->setTimezone("Europe/London");
  load();
  TEST_ASSERT_TRUE(sandbox->loadChunk(kChecks));
}

// Run a chunk of checks; every `is` must hold.
void check(const char* chunk) {
  TEST_ASSERT_TRUE_MESSAGE(sandbox->loadChunk(chunk), "chunk raised");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, sandbox->luaGlobalIntForTest("nfail"),
                                "a check failed: see MISMATCH above");
}

size_t count(const std::string& hay, const char* needle) {
  size_t n = 0;
  for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
  return n;
}

}  // namespace

void setUp(void) {
  testMillis() = 0;
  ezTimeStub::reset();
  testSerialLog().clear();
}
void tearDown(void) { delete sandbox; sandbox = nullptr; }

// ── the core it adds to ──────────────────────────────────────────────────

void test_core_prints_no_zone_for_a_date(void) {
  Resident::timecore::Tm t = Resident::timecore::breakDown(kNow, 3600, 1, "BST");
  t.hasZone = false;
  char buf[64];
  Resident::timecore::format(buf, sizeof(buf), "[%Z][%z] %H", t);
  TEST_ASSERT_EQUAL_STRING("[][] 13", buf);
}

// ── construction and fields ──────────────────────────────────────────────

void test_fields_round_trip(void) {
  build();
  check(
      "local t = datetime(2026, 10, 5, 12, 5, 9)\n"
      "is(t.year, 2026, 'year') is(t.month, 10, 'month') is(t.day, 5, 'day')\n"
      "is(t.hour, 12, 'hour') is(t.minute, 5, 'minute') is(t.second, 9, 'second')\n"
      "is(math.type(t.second), 'integer', 'integer fields')\n"
      "is(t.tzinfo == datetime.UTC, false, 'constructed local')\n"
      "local u = datetime.datetime(2026, 10, 5, 12, 5, 9, datetime.UTC)\n"
      "is(u.tzinfo, datetime.UTC, 'tz argument') is(datetime.timezone.utc, datetime.UTC, 'timezone.utc')\n"
      "is(datetime(2026, 12, 25).hour, 0, 'time defaults to midnight')\n"
      "local d = datetime.date(2026, 12, 25)\n"
      "is(d.year, 2026, 'date year') is(d.month, 12, 'date month') is(d.day, 25, 'date day')\n"
      "is(d.hour, nil, 'a date has no hour')\n"
      "is(datetime.datetime.now == datetime.now, true, 'datetime.datetime is the module')\n"
      "is(datetime.date.today(), datetime.today(), 'date.today')\n");
}

void test_now_today_fromtimestamp_synced(void) {
  build();
  check(
      "is(datetime.synced(), true, 'synced')\n"
      "local n = datetime.now()\n"
      "is(tostring(n), '2026-10-05 13:05:09', 'now is local')\n"
      "is(n:tzname(), 'BST', 'local name')\n"
      "is(tostring(datetime.now(datetime.UTC)), '2026-10-05 12:05:09', 'now in UTC')\n"
      "is(n:timestamp(), 1791201909, 'timestamp')\n"
      "is(math.type(n:timestamp()), 'integer', 'integer timestamp')\n"
      "is(tostring(datetime.today()), '2026-10-05', 'today is a date')\n"
      "is(datetime.today() == n:date(), true, 'today == now:date()')\n"
      "is(tostring(datetime.fromtimestamp(1774745999)), '2026-03-29 00:59:59', 'before spring')\n"
      "is(tostring(datetime.fromtimestamp(1774746000)), '2026-03-29 02:00:00', 'after spring')\n"
      "is(tostring(datetime.fromtimestamp(1792889999)), '2026-10-25 01:59:59', 'before autumn')\n"
      "is(tostring(datetime.fromtimestamp(1792890000)), '2026-10-25 01:00:00', 'after autumn')\n"
      "is(tostring(datetime.fromtimestamp(1774746000, datetime.UTC)), '2026-03-29 01:00:00', 'fromtimestamp UTC')\n");
}

void test_unsynced_reads_1970_and_says_so(void) {
  build(false);
  ezTimeStub::state().synced = false;
  ezTimeStub::state().epoch = 0;
  check(
      "is(datetime.synced(), false, 'not synced')\n"
      "is(datetime.now().year, 1970, 'the epoch')\n"
      "is(datetime.now():tzname(), 'UTC', 'UTC until a zone is set')\n");
}

// ── calendar ─────────────────────────────────────────────────────────────

void test_weekday_isoweekday_ordinal(void) {
  build();
  check(
      "local function w(y, m, d, wd, iso, o)\n"
      "  local x = datetime.date(y, m, d)\n"
      "  is(x:weekday(), wd, tostring(x) .. ' weekday')\n"
      "  is(x:isoweekday(), iso, tostring(x) .. ' isoweekday')\n"
      "  is(x:toordinal(), o, tostring(x) .. ' ordinal')\n"
      "  is(datetime.date.fromordinal(o), x, tostring(x) .. ' fromordinal')\n"
      "end\n"
      "w(2026, 10, 5, 0, 1, 739894)\n"
      "w(2000, 2, 29, 1, 2, 730179)\n"
      "w(1, 1, 1, 0, 1, 1)\n"
      "w(9999, 12, 31, 4, 5, 3652059)\n"
      "w(1970, 1, 1, 3, 4, 719163)\n"
      "w(2026, 12, 25, 4, 5, 739975)\n"
      "is(datetime(2026, 10, 5, 23, 59):weekday(), 0, 'datetime weekday')\n");
}

void test_strftime_isoformat_tostring(void) {
  build();
  check(
      "local u = datetime(2026, 10, 5, 12, 5, 9, datetime.UTC)\n"
      "is(u:strftime('%a %A %b %B %d %H %I %j %m %M %p %S %w %y %Y %Z %z %%'),\n"
      "   'Mon Monday Oct October 05 12 12 278 10 05 PM 09 1 26 2026 UTC +0000 %', 'strftime')\n"
      "is(u:strftime('%c|%x|%X|%U %W'), 'Mon Oct  5 12:05:09 2026|10/05/26|12:05:09|40 40', 'strftime composites')\n"
      "is(datetime.date(2026, 10, 5):strftime('%a %d %b %Y [%Z][%z] %H:%M:%S'),\n"
      "   'Mon 05 Oct 2026 [][] 00:00:00', 'a date has no zone')\n"
      "is(datetime(2026, 10, 5, 13, 5, 9):strftime('%H:%M %Z %z'), '13:05 BST +0100', 'local strftime')\n"
      "is(u:isoformat(), '2026-10-05T12:05:09+00:00', 'isoformat UTC')\n"
      "is(u:isoformat(' '), '2026-10-05 12:05:09+00:00', 'isoformat sep')\n"
      "is(datetime(2026, 10, 5, 12, 5, 9):isoformat(), '2026-10-05T12:05:09+01:00', 'isoformat BST')\n"
      "is(datetime(2026, 1, 5, 12, 5, 9):isoformat(), '2026-01-05T12:05:09+00:00', 'isoformat GMT')\n"
      "is(tostring(u), '2026-10-05 12:05:09', 'tostring datetime')\n"
      "is(tostring(datetime.date(2026, 10, 5)), '2026-10-05', 'tostring date')\n"
      "is(datetime.date(2026, 10, 5):isoformat(), '2026-10-05', 'date isoformat')\n"
      "is(tostring(datetime.date(1, 1, 1)), '0001-01-01', 'four-digit year')\n");
}

void test_replace_and_astimezone(void) {
  build();
  check(
      "local t = datetime(2026, 10, 5, 12, 5, 9)\n"
      "is(tostring(t:replace{ hour = 0, minute = 0, second = 0 }), '2026-10-05 00:00:00', 'replace time')\n"
      "is(tostring(t:replace{ day = 31 }), '2026-10-31 12:05:09', 'replace day')\n"
      "is(t:replace{ tzinfo = datetime.UTC }.tzinfo, datetime.UTC, 'replace relabels the zone')\n"
      "is(tostring(datetime.date(2024, 2, 29):replace{ year = 2028 }), '2028-02-29', 'date replace')\n"
      "is(err(function() return datetime.date(2024, 2, 29):replace{ year = 2026 } end),\n"
      "   'date:replace: day must be 1..28', 'replace checks')\n"
      "local u = datetime(2026, 10, 5, 12, 5, 9, datetime.UTC)\n"
      "is(tostring(u:astimezone()), '2026-10-05 13:05:09', 'to local')\n"
      "is(u:astimezone(datetime.UTC) == u, true, 'same zone is itself')\n"
      "is(tostring(datetime(2026, 1, 5, 9):astimezone(datetime.UTC)), '2026-01-05 09:00:00', 'GMT to UTC')\n"
      "is(tostring(u:utcoffset()), '0:00:00', 'UTC offset')\n"
      "is(tostring(datetime(2026, 10, 5):utcoffset()), '1:00:00', 'BST offset')\n");
}

// ── timedelta ────────────────────────────────────────────────────────────

void test_timedelta_normalises_as_python(void) {
  build();
  check(
      "local td = datetime.timedelta\n"
      "local function n(x, d, s, str, total, what)\n"
      "  is(x.days, d, what .. ' days') is(x.seconds, s, what .. ' seconds')\n"
      "  is(tostring(x), str, what .. ' str') is(x:total_seconds(), total, what .. ' total')\n"
      "end\n"
      "n(td{ seconds = -1 }, -1, 86399, '-1 day, 23:59:59', -1, 'seconds=-1')\n"
      "n(td{ hours = 25 }, 1, 3600, '1 day, 1:00:00', 90000, 'hours=25')\n"
      "n(td{ weeks = 1, days = -1, hours = -1, minutes = 90, seconds = -30 }, 6, 1770, '6 days, 0:29:30', 520170, 'mixed')\n"
      "n(td{ hours = 1.5 }, 0, 5400, '1:30:00', 5400, 'hours=1.5')\n"
      "n(td{ days = -1.5 }, -2, 43200, '-2 days, 12:00:00', -129600, 'days=-1.5')\n"
      "n(td{ days = 2 }, 2, 0, '2 days, 0:00:00', 172800, 'days=2')\n"
      "n(td(0), 0, 0, '0:00:00', 0, 'zero')\n"
      "n(td(), 0, 0, '0:00:00', 0, 'empty')\n"
      "n(-td{ days = 1, hours = 2 }, -2, 79200, '-2 days, 22:00:00', -93600, 'negated')\n"
      "n(td{ minutes = -1441 }, -2, 86340, '-2 days, 23:59:00', -86460, 'minutes=-1441')\n"
      "n(td(1), 1, 0, '1 day, 0:00:00', 86400, 'bare number = days')\n"
      "n(td(1, 7200), 1, 7200, '1 day, 2:00:00', 93600, 'days, seconds')\n"
      "local a = td{ days = 1, hours = 2 }\n"
      "is(tostring(a * 3), '3 days, 6:00:00', 'td * 3')\n"
      "is(tostring(3 * a), '3 days, 6:00:00', '3 * td')\n"
      "is(tostring(a + td{ hours = 23 }), '2 days, 1:00:00', 'td + td')\n"
      "is(tostring(a - td{ days = 2 }), '-1 day, 2:00:00', 'td - td')\n"
      "is(tostring(a * -2), '-3 days, 20:00:00', 'td * -2')\n"
      "is(tostring(a * 0), '0:00:00', 'td * 0')\n"
      "is(td{ hours = 23 } < td{ days = 1 }, true, 'td <')\n"
      "is(td{ seconds = -1 } < td(0), true, 'negative <')\n"
      "is(td{ hours = 24 } == td{ days = 1 }, true, 'td ==')\n"
      "is(td{ weeks = 5000 } * 100 > td(0), true, 'mul keeps its range')\n");
}

// ── arithmetic ───────────────────────────────────────────────────────────

void test_date_plus_minus_timedelta_across_ends(void) {
  build();
  check(
      "local D, td = datetime.date, datetime.timedelta\n"
      "local function plus(y, m, d, n, want)\n"
      "  is(tostring(D(y, m, d) + td{ days = n }), want, ('%04d-%02d-%02d %+d'):format(y, m, d, n))\n"
      "end\n"
      "plus(2026, 1, 31, 1, '2026-02-01')\n"
      "plus(2024, 2, 28, 1, '2024-02-29')\n"
      "plus(2023, 2, 28, 1, '2023-03-01')\n"
      "plus(2026, 12, 31, 1, '2027-01-01')\n"
      "plus(2024, 3, 1, -1, '2024-02-29')\n"
      "plus(1900, 2, 28, 1, '1900-03-01')\n"
      "plus(2026, 10, 5, 81, '2026-12-25')\n"
      "plus(2026, 10, 5, -365, '2025-10-05')\n"
      "is(tostring(td(1) + D(2026, 12, 31)), '2027-01-01', 'td + date')\n"
      "is(tostring(D(2026, 12, 31) - td(1)), '2026-12-30', 'date - td')\n"
      "is(tostring(D(2026, 1, 2) - td{ seconds = 1 }), '2026-01-02', 'a date uses .days')\n"
      "is(tostring(D(2026, 1, 2) + td{ hours = 23 }), '2026-01-02', 'hours under a day')\n"
      "is(tostring(D(2026, 1, 2) - td{ hours = 25 }), '2026-01-01', 'hours over a day')\n"
      "is(tostring(D(2026, 12, 25) - D(2026, 10, 5)), '81 days, 0:00:00', 'date - date')\n"
      "is((D(2026, 12, 25) - D(2026, 10, 5)).days, 81, 'days until')\n"
      "is(tostring(D(2026, 10, 5) - D(2026, 12, 25)), '-81 days, 0:00:00', 'negative')\n"
      "is((D(2027, 3, 1) - D(2026, 3, 1)).days, 365, 'a plain year')\n"
      "is((D(2025, 3, 1) - D(2024, 3, 1)).days, 365, 'over a leap day, from March')\n"
      "is(err(function() return D(9999, 12, 31) + td(1) end),\n"
      "   'datetime: result out of range (years 1..9999)', 'past 9999')\n");
}

void test_datetime_arithmetic_and_dst(void) {
  build();
  check(
      "local td, U = datetime.timedelta, datetime.UTC\n"
      // Spring: noon plus a day is noon, and the offset re-resolves.
      "local a = datetime(2026, 3, 28, 12)\n"
      "local b = a + td(1)\n"
      "is(tostring(b), '2026-03-29 12:00:00', 'wall-clock day')\n"
      "is(a:tzname(), 'GMT', 'before') is(b:tzname(), 'BST', 'after')\n"
      "is(tostring(a:utcoffset()), '0:00:00', 'GMT offset') is(tostring(b:utcoffset()), '1:00:00', 'BST offset')\n"
      "is(a:timestamp(), 1774699200, 'a ts') is(b:timestamp(), 1774782000, 'b ts')\n"
      "is(tostring(b - a), '1 day, 0:00:00', 'same zone: on the wall')\n"
      "is(tostring(b:astimezone(U) - a:astimezone(U)), '23:00:00', 'in UTC: 23 hours')\n"
      "is(b:timestamp() - a:timestamp(), 82800, 'timestamps')\n"
      // Autumn: two days on the wall is 49 hours of real time.
      "local c, e = datetime(2026, 10, 24, 12), datetime(2026, 10, 26, 12)\n"
      "is(tostring(e - c), '2 days, 0:00:00', 'autumn, same zone')\n"
      "is(tostring(e:astimezone(U) - c), '2 days, 1:00:00', 'cross zone')\n"
      "is(tostring(e - c:astimezone(U)), '2 days, 1:00:00', 'cross zone, other way')\n"
      "is(c:isoformat(), '2026-10-24T12:00:00+01:00', 'c iso') is(e:isoformat(), '2026-10-26T12:00:00+00:00', 'e iso')\n"
      "is(tostring(datetime(2026, 10, 5, 23, 30) + td{ hours = 1 }), '2026-10-06 00:30:00', 'past midnight')\n"
      "is(tostring(datetime(9999, 12, 31, 23, 59, 59, U) - datetime(1, 1, 1, 0, 0, 0, U)),\n"
      "   '3652058 days, 23:59:59', 'the whole range, no overflow')\n"
      "is(tostring(datetime(2026, 10, 5, 12) - td{ seconds = 1 }), '2026-10-05 11:59:59', 'dt - td')\n");
}

void test_comparisons(void) {
  build();
  check(
      "local U, D = datetime.UTC, datetime.date\n"
      "is(datetime(2026, 10, 5, 12, 0, 0, U) == datetime(2026, 10, 5, 13), true, 'equal across zones')\n"
      "is(datetime(2026, 10, 5, 12, 0, 0, U) < datetime(2026, 10, 5, 12, 30), false, '12:30 BST is 11:30 UTC')\n"
      "is(datetime(2026, 10, 5, 11, 0, 0, U) < datetime(2026, 10, 5, 12, 30), true, 'cross-zone <')\n"
      "is(datetime(2026, 10, 5, 12) <= datetime(2026, 10, 5, 12), true, '<=')\n"
      "is(datetime(2026, 10, 5, 12) > datetime(2026, 10, 5, 11, 59, 59), true, '>')\n"
      "is(D(2026, 10, 5) < D(2026, 12, 25), true, 'date <')\n"
      "is(D(2026, 10, 5) == D(2026, 10, 5), true, 'date ==')\n"
      "is(D(2026, 10, 5) == datetime(2026, 10, 5), false, 'a date never equals a datetime')\n"
      "is(datetime(2026, 10, 5):date() == D(2026, 10, 5), true, 'dt:date() ==')\n"
      "is(err(function() return D(2026, 10, 5) < datetime(2026, 10, 5) end),\n"
      "   \"can't compare date to datetime\", 'date < datetime raises')\n"
      "is(err(function() return datetime.timedelta(1) < 5 end),\n"
      "   \"can't compare timedelta to number\", 'timedelta < number raises')\n");
}

// ── the reach of int32 ───────────────────────────────────────────────────

void test_epoch_seconds_stop_at_2038(void) {
  build();
  check(
      "local U = datetime.UTC\n"
      "local limit = 'datetime: outside 1901-12-13..2038-01-19, the reach of 32-bit epoch seconds'\n"
      "is(datetime(2038, 1, 19, 3, 14, 7, U):timestamp(), 2147483647, 'the last second')\n"
      "is(datetime(1901, 12, 13, 20, 45, 52, U):timestamp(), -2147483648, 'the first second')\n"
      "is(err(function() return datetime(2038, 1, 19, 3, 14, 8, U):timestamp() end), limit, 'one past')\n"
      "is(err(function() return datetime(2040, 1, 1):utcoffset() end), limit, 'a local offset past it')\n"
      "is(err(function() return datetime(2040, 1, 1) == datetime(2040, 1, 1, 0, 0, 0, U) end), limit, 'cross zone past it')\n"
      // Nothing that stays on the wall clock needs the epoch.
      "local far = datetime(2040, 1, 1)\n"
      "is(tostring(far + datetime.timedelta(1)), '2040-01-02 00:00:00', 'wall arithmetic past it')\n"
      "is((far - datetime(2026, 10, 5)).days, 4836, 'same-zone difference past it')\n"
      "is(far:strftime('%Y-%m-%d %H:%M'), '2040-01-01 00:00', 'strftime without %z past it')\n"
      "is(datetime.date(9999, 12, 31):weekday(), 4, 'dates run to 9999')\n");
}

// ── argument errors name the call ────────────────────────────────────────

void test_bad_arguments_raise_naming_the_call(void) {
  build();
  check(
      "local function bad(want, fn, ...) is(err(fn, ...), want, want) end\n"
      "bad('datetime.date: month must be 1..12', datetime.date, 2026, 13, 1)\n"
      "bad('datetime.date: day must be 1..28', datetime.date, 2026, 2, 29)\n"
      "bad('datetime.date: year must be 1..9999', datetime.date, 0, 1, 1)\n"
      "bad('datetime.date: year must be 1..9999', datetime.date, '2026', 1, 1)\n"
      "bad('datetime.date: day must be 1..31', datetime.date, 2026, 1, 1.5)\n"
      "bad('datetime: hour must be 0..23', datetime, 2026, 1, 1, 24)\n"
      "bad('datetime: tz must be datetime.UTC, or nil for local', datetime, 2026, 1, 1, 0, 0, 0, 'UTC')\n"
      "bad('datetime.now: tz must be datetime.UTC, or nil for local', datetime.now, 'UTC')\n"
      "bad(\"datetime.timedelta: no field 'hour' (weeks, days, hours, minutes, seconds)\", datetime.timedelta, { hour = 1 })\n"
      "bad('datetime.timedelta: days must be a number', datetime.timedelta, '1')\n"
      "bad('datetime.fromtimestamp: secs must be a number', datetime.fromtimestamp, nil)\n"
      "bad(\"datetime:replace: no field 'hours'\", datetime(2026, 1, 1).replace, datetime(2026, 1, 1), { hours = 1 })\n"
      "bad('timedelta * n: n must be an integer', function() return datetime.timedelta(1) * 1.5 end)\n"
      "bad(\"unsupported operand types for +: 'date' and 'number'\", function() return datetime.date(2026, 1, 1) + 1 end)\n"
      "bad(\"unsupported operand types for -: 'date' and 'datetime'\", function() return datetime.date(2026, 1, 1) - datetime(2026, 1, 1) end)\n"
      "bad('datetime:strftime: format must be a string', datetime(2026, 1, 1).strftime, datetime(2026, 1, 1))\n");
  // An argument error points at the app's line, not inside the module.
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local ok, where = pcall(function() local d = datetime.date(2026, 13, 1) return d end)\n"
      "pointed = where:find('^%[string') ~= nil\n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("pointed"));
}

// ── loading: on first touch, fresh per app ───────────────────────────────

void test_loads_on_first_touch_and_afresh_per_app(void) {
  build();
  check(
      "is(rawget(datetime, 'now'), nil, 'not loaded until touched')\n"
      "local early = datetime\n"
      "collectgarbage() collectgarbage()\n"
      "local before = collectgarbage('count')\n"
      "is(type(datetime.now), 'function', 'an index loads it')\n"
      "collectgarbage() collectgarbage()\n"
      "heap_bytes = math.floor((collectgarbage('count') - before) * 1024)\n"
      "is(early.now == datetime.now, true, 'a reference taken before is the module')\n"
      "is(rawget(datetime, 'now') ~= nil, true, 'loaded into itself')\n"
      "datetime.now = nil\n");
  printf("datetime module, loaded: %d bytes of Lua heap\n", sandbox->luaGlobalIntForTest("heap_bytes"));
  // A new app gets a fresh stub: unloaded, and without the last app's edit.
  load();
  TEST_ASSERT_TRUE(sandbox->loadChunk(kChecks));
  check(
      "is(rawget(datetime, 'now'), nil, 'unloaded again')\n"
      "is(type(datetime.now), 'function', 'and whole')\n");
  // A call is a first touch too.
  load();
  TEST_ASSERT_TRUE(sandbox->loadChunk(kChecks));
  check("is(tostring(datetime(2026, 12, 25)), '2026-12-25 00:00:00', 'a call loads it')\n");
}

void test_datetime_works_in_an_app_init(void) {
  build();
  load(
      "function init(ctx)\n"
      "  left = (datetime.date(2026, 12, 25) - datetime.today()).days\n"
      "  tomorrow = tostring(datetime.today() + datetime.timedelta{ days = 1 })\n"
      "end\n"
      "function on_tick(ctx, dt) end\n");
  TEST_ASSERT_EQUAL_INT(81, sandbox->luaGlobalIntForTest("left"));
  TEST_ASSERT_TRUE(sandbox->loadChunk("ok = tomorrow == '2026-10-06'\n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("ok"));
}

// ── time's calendar half: deprecated, once per load ──────────────────────

void test_time_calendar_calls_warn_once_per_load(void) {
  build();
  testSerialLog().clear();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "for _ = 1, 3 do\n"
      "  time.time() time.gmtime() time.localtime() time.mktime(time.localtime())\n"
      "  time.strftime('%H') time.synced()\n"
      "end\n"));
  const std::string& log = testSerialLog();
  TEST_ASSERT_EQUAL_UINT(1, count(log, "[WARN] [deprecated] time.time(): use datetime.now():timestamp()\n"));
  TEST_ASSERT_EQUAL_UINT(1, count(log, "[WARN] [deprecated] time.localtime(): use datetime.now()"));
  TEST_ASSERT_EQUAL_UINT(1, count(log, "[WARN] [deprecated] time.gmtime(): use datetime.now(datetime.UTC)"));
  TEST_ASSERT_EQUAL_UINT(1, count(log, "[WARN] [deprecated] time.mktime(): use datetime("));
  TEST_ASSERT_EQUAL_UINT(1, count(log, "[WARN] [deprecated] time.strftime(): use dt:strftime(fmt)\n"));
  TEST_ASSERT_EQUAL_UINT(1, count(log, "[WARN] [deprecated] time.synced(): use datetime.synced()\n"));
  TEST_ASSERT_EQUAL_UINT(6, count(log, "[deprecated]"));
  // The next app load owes them again.
  load();
  testSerialLog().clear();
  TEST_ASSERT_TRUE(sandbox->loadChunk("time.localtime() time.localtime()\n"));
  TEST_ASSERT_EQUAL_UINT(1, count(testSerialLog(), "[deprecated] time.localtime()"));
}

void test_ticks_and_datetime_never_warn(void) {
  build();
  testSerialLog().clear();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local a = time.ticks_ms() local d = time.ticks_diff(time.ticks_ms(), a)\n"
      "local n = datetime.now() local s = n:strftime('%H') local t = n:timestamp()\n"
      "local y = datetime.synced() local f = datetime.fromtimestamp(0)\n"));
  TEST_ASSERT_EQUAL_UINT(0, count(testSerialLog(), "[deprecated]"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_core_prints_no_zone_for_a_date);
  RUN_TEST(test_fields_round_trip);
  RUN_TEST(test_now_today_fromtimestamp_synced);
  RUN_TEST(test_unsynced_reads_1970_and_says_so);
  RUN_TEST(test_weekday_isoweekday_ordinal);
  RUN_TEST(test_strftime_isoformat_tostring);
  RUN_TEST(test_replace_and_astimezone);
  RUN_TEST(test_timedelta_normalises_as_python);
  RUN_TEST(test_date_plus_minus_timedelta_across_ends);
  RUN_TEST(test_datetime_arithmetic_and_dst);
  RUN_TEST(test_comparisons);
  RUN_TEST(test_epoch_seconds_stop_at_2038);
  RUN_TEST(test_bad_arguments_raise_naming_the_call);
  RUN_TEST(test_loads_on_first_touch_and_afresh_per_app);
  RUN_TEST(test_datetime_works_in_an_app_init);
  RUN_TEST(test_time_calendar_calls_warn_once_per_load);
  RUN_TEST(test_ticks_and_datetime_never_warn);
  return UNITY_END();
}
