// Render targets + bind-is-the-claim ownership.
//
// The registry is pure enough to test host-side in full: panel registration,
// same-name merging, the owner bit, last-claim-wins, the strict gate (an
// UNOWNED target answers false for everyone — that is what stops a wiped
// LVGL tree flushing a blank frame behind the incoming app), and the
// per-module release that each module performs on onAppReset.
#include <unity.h>
#include <cstdio>
#include "ResidentRenderTargets.h"

namespace {

using Resident::RenderTargets;

class FakePanel : public Resident::PanelTarget {
public:
  FakePanel(int32_t w, int32_t h) : _w(w), _h(h) {}
  int32_t width() const override { sizeReads++; return _w; }
  int32_t height() const override { sizeReads++; return _h; }
  mutable int sizeReads = 0;
  void blit(int32_t x, int32_t y, int32_t w, int32_t h,
            const uint16_t* px) override {
    blits++;
    lastX = x; lastY = y; lastW = w; lastH = h; lastPx = px;
  }
  int blits = 0;
  int32_t lastX = -1, lastY = -1, lastW = -1, lastH = -1;
  const uint16_t* lastPx = nullptr;

private:
  int32_t _w, _h;
};

}  // namespace

void setUp(void) { RenderTargets::clear(); }
void tearDown(void) { RenderTargets::clear(); }

void test_add_panel_registers_geometry(void) {
  FakePanel panel(240, 135);
  TEST_ASSERT_TRUE(RenderTargets::addPanel("main", &panel, "rect"));
  TEST_ASSERT_EQUAL_INT(1, RenderTargets::count());
  // Geometry is READ, not cached at registration: a board registers during
  // static init, where asking the panel its size is a crash.
  int32_t w = 0, h = 0;
  RenderTargets::size(RenderTargets::entry(0), w, h);
  TEST_ASSERT_EQUAL_INT(240, (int)w);
  TEST_ASSERT_EQUAL_INT(135, (int)h);
  TEST_ASSERT_EQUAL_STRING("rect", RenderTargets::entry(0).shape);
  TEST_ASSERT_EQUAL_PTR(&panel, RenderTargets::panel("main"));
  TEST_ASSERT_NULL(RenderTargets::panel("nope"));
  // A board registration declares no module: modules declare themselves.
  TEST_ASSERT_EQUAL_UINT8(0, RenderTargets::entry(0).modules);
}

// The crash this guards: a board registers its panel in its config function,
// which runs during static init — before M5.begin(), before any display
// driver's begin(). Asking that panel how big it is dereferences hardware
// that does not exist yet. Registration must not ask.
void test_add_panel_never_asks_the_panel_its_size(void) {
  FakePanel panel(240, 135);
  panel.sizeReads = 0;
  RenderTargets::addPanel("main", &panel);
  TEST_ASSERT_EQUAL_INT(0, panel.sizeReads);
  // ...and a reader gets the real numbers anyway.
  int32_t w = 0, h = 0;
  RenderTargets::size(RenderTargets::entry(0), w, h);
  TEST_ASSERT_EQUAL_INT(240, (int)w);
  TEST_ASSERT_EQUAL_INT(135, (int)h);
  TEST_ASSERT_GREATER_THAN_INT(0, panel.sizeReads);
}

// The same rule one layer up: a graphics module's addDisplay is called from
// the same board config function, so declaring must not measure either.
void test_module_declare_never_asks_the_panel_its_size(void) {
  FakePanel panel(240, 135);
  RenderTargets::addPanel("main", &panel);
  panel.sizeReads = 0;
  RenderTargets::declare("main", RenderTargets::MODULE_LGFX);
  RenderTargets::declare("main", RenderTargets::MODULE_LVGL);
  TEST_ASSERT_EQUAL_INT(0, panel.sizeReads);
  TEST_ASSERT_EQUAL_UINT8(RenderTargets::MODULE_LGFX | RenderTargets::MODULE_LVGL,
                          RenderTargets::entry(0).modules);
}

// The bug this guards, seen on glass: a board declares its panel round, the
// lgfx module's addDisplay defaults `shape = "rect"`, and the module's
// declaration overwrote the board on the very next line. Every consumer then
// believed the round device was rectangular — an app that lays itself out by
// shape drew its whole look off the edge of the circle.
void test_a_module_cannot_flatten_a_boards_round_panel(void) {
  FakePanel panel(466, 466);
  RenderTargets::addPanel("main", &panel, "round");
  RenderTargets::declare("main", RenderTargets::MODULE_LGFX, "rect");
  TEST_ASSERT_EQUAL_STRING("round", RenderTargets::entry(0).shape);
}

// ...but a target with no panel behind it has no board to ask, so there the
// module's shape is the only source and must apply.
void test_a_module_shape_applies_to_a_panelless_target(void) {
  RenderTargets::declare("sprite", RenderTargets::MODULE_LGFX, "round");
  TEST_ASSERT_EQUAL_STRING("round", RenderTargets::entry(0).shape);
}

void test_modules_merge_onto_one_target(void) {
  FakePanel panel(466, 466);
  RenderTargets::addPanel("dial", &panel, "round");
  RenderTargets::add("dial", 466, 466, nullptr, RenderTargets::MODULE_LGFX);
  RenderTargets::add("dial", 466, 466, nullptr, RenderTargets::MODULE_LVGL);
  TEST_ASSERT_EQUAL_INT(1, RenderTargets::count());   // one panel, two modules
  TEST_ASSERT_EQUAL_UINT8(RenderTargets::MODULE_LGFX | RenderTargets::MODULE_LVGL,
                          RenderTargets::entry(0).modules);
  TEST_ASSERT_EQUAL_STRING("round", RenderTargets::entry(0).shape);
  TEST_ASSERT_EQUAL_PTR(&panel, RenderTargets::panel("dial"));
}

void test_unowned_target_answers_false_for_everyone(void) {
  FakePanel panel(240, 135);
  RenderTargets::addPanel("main", &panel);
  TEST_ASSERT_EQUAL_UINT8(0, RenderTargets::owner("main"));
  TEST_ASSERT_FALSE(RenderTargets::isOwner("main", RenderTargets::MODULE_LGFX));
  TEST_ASSERT_FALSE(RenderTargets::isOwner("main", RenderTargets::MODULE_LVGL));
  // Unknown names too — a present path that can't find its target stands down.
  TEST_ASSERT_FALSE(RenderTargets::isOwner("nope", RenderTargets::MODULE_LGFX));
}

void test_bind_claims_and_last_claim_wins(void) {
  FakePanel panel(240, 135);
  RenderTargets::addPanel("main", &panel);

  TEST_ASSERT_TRUE(RenderTargets::claim("main", RenderTargets::MODULE_LGFX));
  TEST_ASSERT_TRUE(RenderTargets::isOwner("main", RenderTargets::MODULE_LGFX));
  TEST_ASSERT_FALSE(RenderTargets::isOwner("main", RenderTargets::MODULE_LVGL));

  // The other library binds: it takes over, the first stands down.
  TEST_ASSERT_TRUE(RenderTargets::claim("main", RenderTargets::MODULE_LVGL));
  TEST_ASSERT_TRUE(RenderTargets::isOwner("main", RenderTargets::MODULE_LVGL));
  TEST_ASSERT_FALSE(RenderTargets::isOwner("main", RenderTargets::MODULE_LGFX));

  // And back again — claiming is not one-way.
  RenderTargets::claim("main", RenderTargets::MODULE_LGFX);
  TEST_ASSERT_TRUE(RenderTargets::isOwner("main", RenderTargets::MODULE_LGFX));

  TEST_ASSERT_FALSE(RenderTargets::claim("nope", RenderTargets::MODULE_LGFX));
}

void test_ownership_is_per_target(void) {
  FakePanel a(240, 135), b(128, 64);
  RenderTargets::addPanel("main", &a);
  RenderTargets::addPanel("aux", &b);
  RenderTargets::claim("main", RenderTargets::MODULE_LVGL);
  RenderTargets::claim("aux", RenderTargets::MODULE_LGFX);
  TEST_ASSERT_TRUE(RenderTargets::isOwner("main", RenderTargets::MODULE_LVGL));
  TEST_ASSERT_TRUE(RenderTargets::isOwner("aux", RenderTargets::MODULE_LGFX));
}

void test_release_is_per_module(void) {
  FakePanel a(240, 135), b(128, 64);
  RenderTargets::addPanel("main", &a);
  RenderTargets::addPanel("aux", &b);
  RenderTargets::claim("main", RenderTargets::MODULE_LVGL);
  RenderTargets::claim("aux", RenderTargets::MODULE_LGFX);

  // Each module releases its own claims in its onAppReset.
  TEST_ASSERT_EQUAL_INT(1, RenderTargets::release(RenderTargets::MODULE_LVGL));
  TEST_ASSERT_EQUAL_UINT8(0, RenderTargets::owner("main"));
  TEST_ASSERT_TRUE(RenderTargets::isOwner("aux", RenderTargets::MODULE_LGFX));
  TEST_ASSERT_EQUAL_INT(1, RenderTargets::release(RenderTargets::MODULE_LGFX));
  TEST_ASSERT_EQUAL_UINT8(0, RenderTargets::owner("aux"));
  TEST_ASSERT_EQUAL_INT(0, RenderTargets::release(RenderTargets::MODULE_LGFX));
}

void test_app_reset_leaves_every_target_unowned(void) {
  FakePanel a(240, 135), b(128, 64);
  RenderTargets::addPanel("main", &a);
  RenderTargets::addPanel("aux", &b);
  RenderTargets::claim("main", RenderTargets::MODULE_LVGL);
  RenderTargets::claim("aux", RenderTargets::MODULE_LGFX);
  RenderTargets::releaseAll();
  // The blank-frame race: after the wipe, NOTHING may present until the
  // incoming app's first bind claims the panel again.
  TEST_ASSERT_FALSE(RenderTargets::isOwner("main", RenderTargets::MODULE_LVGL));
  TEST_ASSERT_FALSE(RenderTargets::isOwner("main", RenderTargets::MODULE_LGFX));
  TEST_ASSERT_FALSE(RenderTargets::isOwner("aux", RenderTargets::MODULE_LGFX));
  // Surfaces are hardware: the entries themselves survive the reset.
  TEST_ASSERT_EQUAL_INT(2, RenderTargets::count());
  TEST_ASSERT_EQUAL_PTR(&a, RenderTargets::panel("main"));
}

void test_panel_blit_receives_the_rect(void) {
  FakePanel panel(240, 135);
  RenderTargets::addPanel("main", &panel);
  uint16_t px[4] = {1, 2, 3, 4};
  RenderTargets::panel("main")->blit(10, 20, 2, 2, px);
  TEST_ASSERT_EQUAL_INT(1, panel.blits);
  TEST_ASSERT_EQUAL_INT(10, (int)panel.lastX);
  TEST_ASSERT_EQUAL_INT(20, (int)panel.lastY);
  TEST_ASSERT_EQUAL_INT(2, (int)panel.lastW);
  TEST_ASSERT_EQUAL_INT(2, (int)panel.lastH);
  TEST_ASSERT_EQUAL_PTR(px, panel.lastPx);
}

void test_table_is_bounded(void) {
  FakePanel panel(8, 8);
  char names[RenderTargets::MAX + 2][8];
  for (int i = 0; i < RenderTargets::MAX + 2; i++) {
    snprintf(names[i], sizeof(names[i]), "t%d", i);
    bool ok = RenderTargets::addPanel(names[i], &panel);
    TEST_ASSERT_EQUAL_INT(i < RenderTargets::MAX, (int)ok);
  }
  TEST_ASSERT_EQUAL_INT(RenderTargets::MAX, RenderTargets::count());
  TEST_ASSERT_FALSE(RenderTargets::addPanel("late", nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_add_panel_registers_geometry);
  RUN_TEST(test_add_panel_never_asks_the_panel_its_size);
  RUN_TEST(test_module_declare_never_asks_the_panel_its_size);
  RUN_TEST(test_a_module_cannot_flatten_a_boards_round_panel);
  RUN_TEST(test_a_module_shape_applies_to_a_panelless_target);
  RUN_TEST(test_modules_merge_onto_one_target);
  RUN_TEST(test_unowned_target_answers_false_for_everyone);
  RUN_TEST(test_bind_claims_and_last_claim_wins);
  RUN_TEST(test_ownership_is_per_target);
  RUN_TEST(test_release_is_per_module);
  RUN_TEST(test_app_reset_leaves_every_target_unowned);
  RUN_TEST(test_panel_blit_receives_the_rect);
  RUN_TEST(test_table_is_bounded);
  return UNITY_END();
}
