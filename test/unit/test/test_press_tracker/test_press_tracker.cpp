// PressTracker: button edges replayed into tap, hold-start and hold-end, with
// every duration taken from the edges' own timestamps.
#include <unity.h>
#include "ResidentButtonEdge.h"

using Resident::ButtonEdge;
using Resident::PressGestures;
using Resident::PressTracker;

namespace {
ButtonEdge down(uint32_t ms) { return {true, ms}; }
ButtonEdge up(uint32_t ms) { return {false, ms}; }
}  // namespace

void setUp() {}
void tearDown() {}

void test_down_alone_is_no_gesture() {
  PressTracker t(500);
  TEST_ASSERT_FALSE(t.edge(down(100)).any());
  TEST_ASSERT_TRUE(t.pressing());
  TEST_ASSERT_FALSE(t.holding());
}

void test_short_press_is_a_tap() {
  PressTracker t(500);
  t.edge(down(100));
  PressGestures g = t.edge(up(250));
  TEST_ASSERT_TRUE(g.tap);
  TEST_ASSERT_FALSE(g.holdStart);
  TEST_ASSERT_FALSE(g.holdEnd);
  TEST_ASSERT_FALSE(t.pressing());
}

void test_poll_fires_hold_start_while_still_down() {
  PressTracker t(500);
  t.edge(down(100));
  TEST_ASSERT_FALSE(t.poll(599).any());      // 499 ms: not yet
  PressGestures g = t.poll(600);              // 500 ms: the threshold
  TEST_ASSERT_TRUE(g.holdStart);
  TEST_ASSERT_FALSE(g.holdEnd);
  TEST_ASSERT_FALSE(g.tap);
  TEST_ASSERT_TRUE(t.holding());
  TEST_ASSERT_FALSE(t.poll(5000).any());     // fires once per press
}

void test_hold_ends_at_release() {
  PressTracker t(500);
  t.edge(down(100));
  t.poll(700);
  PressGestures g = t.edge(up(900));
  TEST_ASSERT_TRUE(g.holdEnd);
  TEST_ASSERT_FALSE(g.holdStart);            // already started by poll
  TEST_ASSERT_FALSE(g.tap);
  TEST_ASSERT_FALSE(t.pressing());
  TEST_ASSERT_FALSE(t.holding());
}

// The loop never polled while the button was down (an app callback held it):
// the up edge alone shows the press lasted past the threshold.
void test_hold_released_before_poll_is_a_hold_not_a_tap() {
  PressTracker t(500);
  t.edge(down(100));
  PressGestures g = t.edge(up(800));
  TEST_ASSERT_TRUE(g.holdStart);
  TEST_ASSERT_TRUE(g.holdEnd);
  TEST_ASSERT_FALSE(g.tap);
}

void test_release_exactly_at_threshold_is_a_hold() {
  PressTracker t(500);
  t.edge(down(100));
  PressGestures g = t.edge(up(600));
  TEST_ASSERT_TRUE(g.holdStart);
  TEST_ASSERT_TRUE(g.holdEnd);
}

void test_unpaired_up_is_ignored() {
  PressTracker t(500);
  TEST_ASSERT_FALSE(t.edge(up(100)).any());
  TEST_ASSERT_FALSE(t.pressing());
  // And the tracker still works afterwards.
  t.edge(down(200));
  TEST_ASSERT_TRUE(t.edge(up(250)).tap);
}

void test_repeated_down_keeps_the_first_press() {
  PressTracker t(500);
  t.edge(down(100));
  TEST_ASSERT_FALSE(t.edge(down(400)).any());
  // Timed from the first down edge: 100 → 650 is a hold.
  PressGestures g = t.edge(up(650));
  TEST_ASSERT_TRUE(g.holdStart);
  TEST_ASSERT_TRUE(g.holdEnd);
}

void test_reset_drops_the_press_and_its_up_edge() {
  PressTracker t(500);
  t.edge(down(100));
  t.poll(700);
  TEST_ASSERT_TRUE(t.holding());
  t.reset();
  TEST_ASSERT_FALSE(t.pressing());
  TEST_ASSERT_FALSE(t.holding());
  TEST_ASSERT_FALSE(t.edge(up(900)).any());
  TEST_ASSERT_FALSE(t.poll(5000).any());
}

// Two quick taps replayed together after a long gap: each is timed from its
// own edges, so neither is a hold, though the button was down at both ends of
// the gap.
void test_two_quick_taps_across_a_gap_are_two_taps() {
  PressTracker t(500);
  t.edge(down(50));
  TEST_ASSERT_FALSE(t.poll(60).any());
  // The loop is away until 700.
  TEST_ASSERT_TRUE(t.edge(up(120)).tap);
  TEST_ASSERT_FALSE(t.edge(down(600)).any());
  TEST_ASSERT_FALSE(t.poll(700).any());      // 100 ms into the second press
  TEST_ASSERT_TRUE(t.edge(up(680)).tap);
}

// The caller read the clock, then an edge arrived stamped later: the press has
// not been held for a negative time that wraps to a huge one.
void test_poll_time_before_the_down_edge_is_no_time_held() {
  PressTracker t(500);
  t.edge(down(1000));
  TEST_ASSERT_FALSE(t.poll(999).any());
  TEST_ASSERT_FALSE(t.holding());
}

void test_durations_survive_millis_wrap() {
  PressTracker t(500);
  t.edge(down(0xFFFFFF00u));                 // 256 ms before the wrap
  TEST_ASSERT_FALSE(t.poll(0x00000010u).any());  // 272 ms held
  TEST_ASSERT_TRUE(t.poll(0x00000200u).holdStart);  // 768 ms held
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_down_alone_is_no_gesture);
  RUN_TEST(test_short_press_is_a_tap);
  RUN_TEST(test_poll_fires_hold_start_while_still_down);
  RUN_TEST(test_hold_ends_at_release);
  RUN_TEST(test_hold_released_before_poll_is_a_hold_not_a_tap);
  RUN_TEST(test_release_exactly_at_threshold_is_a_hold);
  RUN_TEST(test_unpaired_up_is_ignored);
  RUN_TEST(test_repeated_down_keeps_the_first_press);
  RUN_TEST(test_reset_drops_the_press_and_its_up_edge);
  RUN_TEST(test_two_quick_taps_across_a_gap_are_two_taps);
  RUN_TEST(test_poll_time_before_the_down_edge_is_no_time_held);
  RUN_TEST(test_durations_survive_millis_wrap);
  UNITY_END();
  return 0;
}
