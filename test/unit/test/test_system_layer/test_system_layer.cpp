// SystemLayerGate: when the firmware's LVGL system layer may render, when its
// frames may reach the glass, and when the board's own content underneath must
// be put back. One case per row of the decision.
#include <unity.h>
#include "ResidentSystemLayer.h"

namespace {

using Resident::SystemLayerGate;
using Owner = SystemLayerGate::Owner;

SystemLayerGate::Decision decide(Owner owner, bool visible, bool suspended,
                                 bool& drew, bool systemOnly = false) {
  SystemLayerGate::Inputs in;
  in.owner = owner;
  in.visible = visible;
  in.suspended = suspended;
  in.systemOnly = systemOnly;
  return SystemLayerGate::decide(in, drew);
}

}  // namespace

void setUp() {}
void tearDown() {}

// An app owns the target: its frames carry the layer, as without one.
void test_app_owned_renders_and_flushes() {
  bool drew = false;
  auto d = decide(Owner::Lvgl, false, false, drew);
  TEST_ASSERT_TRUE(d.render);
  TEST_ASSERT_TRUE(d.flush);
  TEST_ASSERT_FALSE(d.restore);
}

// Suspended: a suppressed door would swallow frames LVGL thinks it drew.
void test_app_suspended_stands_down() {
  bool drew = false;
  auto d = decide(Owner::Lvgl, true, true, drew);
  TEST_ASSERT_FALSE(d.render);
  TEST_ASSERT_FALSE(d.flush);
  TEST_ASSERT_FALSE(d.restore);
}

// Nobody owns it and the layer is empty: the driver's content stays.
void test_unowned_hidden_is_silent() {
  bool drew = false;
  auto d = decide(Owner::None, false, false, drew);
  TEST_ASSERT_FALSE(d.render);
  TEST_ASSERT_FALSE(d.flush);
  TEST_ASSERT_FALSE(d.restore);
  TEST_ASSERT_FALSE(drew);
}

// Nobody owns it and the layer shows something: draw it over the driver.
void test_unowned_visible_draws_over_the_driver() {
  bool drew = false;
  auto d = decide(Owner::None, true, false, drew);
  TEST_ASSERT_TRUE(d.render);
  TEST_ASSERT_TRUE(d.flush);
  TEST_ASSERT_FALSE(d.restore);
  TEST_ASSERT_TRUE(drew);
}

// ...and when it empties, the driver repaints, once.
void test_unowned_emptying_restores_once() {
  bool drew = false;
  decide(Owner::None, true, false, drew);
  auto d = decide(Owner::None, false, false, drew);
  TEST_ASSERT_TRUE(d.restore);
  TEST_ASSERT_FALSE(d.flush);
  d = decide(Owner::None, false, false, drew);
  TEST_ASSERT_FALSE(d.restore);
}

// An app binding after the layer drew over the driver: nothing to restore
// later, the app owns every pixel now.
void test_app_bind_forgets_the_driver() {
  bool drew = false;
  decide(Owner::None, true, false, drew);
  decide(Owner::Lvgl, true, false, drew);
  TEST_ASSERT_FALSE(drew);
  auto d = decide(Owner::None, false, false, drew);   // app reset, layer empty
  TEST_ASSERT_FALSE(d.restore);
}

// Another library owns the target: the layer stands down entirely.
void test_other_library_stands_down() {
  bool drew = true;
  auto d = decide(Owner::Other, true, false, drew);
  TEST_ASSERT_FALSE(d.render);
  TEST_ASSERT_FALSE(d.flush);
  TEST_ASSERT_FALSE(d.restore);
  TEST_ASSERT_FALSE(drew);
}

// A system-only display: always the layer's.
void test_system_only_always_draws() {
  bool drew = false;
  auto d = decide(Owner::None, false, true, drew, true);
  TEST_ASSERT_TRUE(d.render);
  TEST_ASSERT_TRUE(d.flush);
  TEST_ASSERT_FALSE(d.restore);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_app_owned_renders_and_flushes);
  RUN_TEST(test_app_suspended_stands_down);
  RUN_TEST(test_unowned_hidden_is_silent);
  RUN_TEST(test_unowned_visible_draws_over_the_driver);
  RUN_TEST(test_unowned_emptying_restores_once);
  RUN_TEST(test_app_bind_forgets_the_driver);
  RUN_TEST(test_other_library_stands_down);
  RUN_TEST(test_system_only_always_draws);
  return UNITY_END();
}
