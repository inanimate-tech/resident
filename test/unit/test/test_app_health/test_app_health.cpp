// The pure half of the app quarantine (ResidentAppHealth.h): the crash record
// and decide(), with no sandbox and no platform.
#include <unity.h>
#include <cstring>

#include "ResidentAppHealth.h"

using namespace Resident::app_health;

namespace {

constexpr uint32_t kLimit = 3;

Record armedWith(uint32_t hash, uint32_t count) {
  Record r = fresh();
  arm(r, hash);
  r.count = count;
  seal(r);
  return r;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_hash_is_stable_and_never_zero(void) {
  const char* a = "function init(ctx) end";
  TEST_ASSERT_EQUAL_UINT32(sourceHash(a, strlen(a)), sourceHash(a, strlen(a)));
  TEST_ASSERT_NOT_EQUAL(sourceHash(a, strlen(a)), sourceHash("x", 1));
  TEST_ASSERT_NOT_EQUAL(0u, sourceHash("", 0));
}

void test_garbage_record_is_invalid(void) {
  Record r;
  memset(&r, 0xA5, sizeof(r));
  TEST_ASSERT_FALSE(valid(r));
  r.magic = kMagic;               // the magic alone is not enough
  TEST_ASSERT_FALSE(valid(r));
  TEST_ASSERT_TRUE(valid(fresh()));
}

void test_power_on_starts_fresh(void) {
  Decision d = decide(armedWith(42, 2), ResetKind::PowerOn, kLimit);
  TEST_ASSERT_FALSE(d.tripped);
  TEST_ASSERT_EQUAL_UINT32(0, d.record.count);
  TEST_ASSERT_EQUAL_UINT32(0, d.record.armed);
  TEST_ASSERT_TRUE(valid(d.record));
}

void test_invalid_record_starts_fresh(void) {
  Record r;
  memset(&r, 0x5A, sizeof(r));
  Decision d = decide(r, ResetKind::Abnormal, kLimit);
  TEST_ASSERT_FALSE(d.tripped);
  TEST_ASSERT_EQUAL_UINT32(0, d.record.count);
  TEST_ASSERT_TRUE(valid(d.record));
}

void test_crash_while_armed_counts(void) {
  Decision d = decide(armedWith(42, 0), ResetKind::Abnormal, kLimit);
  TEST_ASSERT_FALSE(d.tripped);
  TEST_ASSERT_EQUAL_UINT32(1, d.record.count);
  TEST_ASSERT_EQUAL_UINT32(42, d.record.hash);
  TEST_ASSERT_EQUAL_UINT32(0, d.record.armed);   // nothing runs at boot
}

void test_crash_while_disarmed_does_not_count(void) {
  Record r = armedWith(42, 1);
  disarm(r);
  Decision d = decide(r, ResetKind::Abnormal, kLimit);
  TEST_ASSERT_FALSE(d.tripped);
  TEST_ASSERT_EQUAL_UINT32(1, d.record.count);
}

void test_deliberate_reset_neither_counts_nor_clears(void) {
  Decision d = decide(armedWith(42, 2), ResetKind::Deliberate, kLimit);
  TEST_ASSERT_FALSE(d.tripped);
  TEST_ASSERT_EQUAL_UINT32(2, d.record.count);
  TEST_ASSERT_EQUAL_UINT32(42, d.record.hash);
}

void test_third_consecutive_crash_trips(void) {
  Record r = fresh();
  for (uint32_t i = 1; i < kLimit; i++) {
    arm(r, 42);
    Decision d = decide(r, ResetKind::Abnormal, kLimit);
    TEST_ASSERT_FALSE(d.tripped);
    TEST_ASSERT_EQUAL_UINT32(i, d.record.count);
    r = d.record;
  }
  arm(r, 42);
  Decision d = decide(r, ResetKind::Abnormal, kLimit);
  TEST_ASSERT_TRUE(d.tripped);
  TEST_ASSERT_EQUAL_UINT32(42, d.hash);
  TEST_ASSERT_EQUAL_UINT32(kLimit, d.crashes);
  TEST_ASSERT_EQUAL_UINT32(0, d.record.count);   // the quarantine holds it now
  TEST_ASSERT_EQUAL_UINT32(0, d.record.hash);
}

void test_limit_zero_never_trips(void) {
  Decision d = decide(armedWith(42, 50), ResetKind::Abnormal, 0);
  TEST_ASSERT_FALSE(d.tripped);
  TEST_ASSERT_EQUAL_UINT32(51, d.record.count);
}

void test_rearming_the_same_app_keeps_its_count(void) {
  Record r = armedWith(42, 2);
  disarm(r);
  arm(r, 42);
  TEST_ASSERT_EQUAL_UINT32(2, r.count);
  TEST_ASSERT_EQUAL_UINT32(1, r.armed);
}

void test_arming_a_different_app_starts_its_own_count(void) {
  Record r = armedWith(42, 2);
  arm(r, 7);
  TEST_ASSERT_EQUAL_UINT32(0, r.count);
  TEST_ASSERT_EQUAL_UINT32(7, r.hash);
}

void test_forgive_clears_the_count_and_stays_armed(void) {
  Record r = armedWith(42, 2);
  forgive(r);
  TEST_ASSERT_EQUAL_UINT32(0, r.count);
  TEST_ASSERT_EQUAL_UINT32(1, r.armed);
  TEST_ASSERT_TRUE(valid(r));
}

void test_mutators_repair_a_garbage_record(void) {
  Record r;
  memset(&r, 0xFF, sizeof(r));
  arm(r, 42);
  TEST_ASSERT_TRUE(valid(r));
  TEST_ASSERT_EQUAL_UINT32(0, r.count);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hash_is_stable_and_never_zero);
  RUN_TEST(test_garbage_record_is_invalid);
  RUN_TEST(test_power_on_starts_fresh);
  RUN_TEST(test_invalid_record_starts_fresh);
  RUN_TEST(test_crash_while_armed_counts);
  RUN_TEST(test_crash_while_disarmed_does_not_count);
  RUN_TEST(test_deliberate_reset_neither_counts_nor_clears);
  RUN_TEST(test_third_consecutive_crash_trips);
  RUN_TEST(test_limit_zero_never_trips);
  RUN_TEST(test_rearming_the_same_app_keeps_its_count);
  RUN_TEST(test_arming_a_different_app_starts_its_own_count);
  RUN_TEST(test_forgive_clears_the_count_and_stays_armed);
  RUN_TEST(test_mutators_repair_a_garbage_record);
  return UNITY_END();
}
