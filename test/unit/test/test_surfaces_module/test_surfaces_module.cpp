// surfaces module: the board's render targets, readable from Lua.
//
// The registry is where a board declares its drawable surfaces; this is the
// read. A consumer that needs to know what surfaces exist and how big they
// are can ASK the device instead of being told out of band — which is the
// whole point, because anything told out of band can be wrong.
#include <unity.h>

#include "ResidentSandbox.cpp"
#include "ResidentRenderTargets.h"

namespace {

using Resident::RenderTargets;

// A panel whose size is settable, so the "geometry comes from the hardware"
// property can be tested rather than assumed.
class FakePanel : public Resident::PanelTarget {
public:
  FakePanel(int32_t w, int32_t h) : _w(w), _h(h) {}
  int32_t width() const override { return _w; }
  int32_t height() const override { return _h; }
  void blit(int32_t, int32_t, int32_t, int32_t, const uint16_t*) override {}
  void resize(int32_t w, int32_t h) { _w = w; _h = h; }

private:
  int32_t _w, _h;
};

Resident::Sandbox* sandbox = nullptr;
FakePanel* main_ = nullptr;
FakePanel* aux = nullptr;

constexpr const char* IDLE_APP = "function on_tick(ctx, dt) end\n";

// Panels are registered BEFORE this (they are board-lifetime, declared at
// firmware setup), so build() must not clear the registry.
void build() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();

  // A chunk needs a running app to run inside.
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = IDLE_APP;
  sandbox->injectMessage("test", "app", doc);
}

}  // namespace

void setUp(void) { testMillis() = 0; RenderTargets::clear(); }

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete main_; main_ = nullptr;
  delete aux; aux = nullptr;
  RenderTargets::clear();
}

void test_lists_nothing_on_a_screenless_board(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk("n = #surfaces.list()\n"));
  TEST_ASSERT_EQUAL_INT(0, sandbox->luaGlobalIntForTest("n"));
}

void test_lists_registered_panels_in_order(void) {
  main_ = new FakePanel(240, 135);
  aux = new FakePanel(64, 64);
  RenderTargets::addPanel("main", main_);
  RenderTargets::addPanel("aux", aux, "round");
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local s = surfaces.list()\n"
      "n = #s\n"
      "names_in_order = (s[1].name == 'main') and (s[2].name == 'aux')\n"
      "w, h = s[1].w, s[1].h\n"
      "shapes = (s[1].shape == 'rect') and (s[2].shape == 'round')\n"));
  TEST_ASSERT_EQUAL_INT(2, sandbox->luaGlobalIntForTest("n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("names_in_order"));
  TEST_ASSERT_EQUAL_INT(240, sandbox->luaGlobalIntForTest("w"));
  TEST_ASSERT_EQUAL_INT(135, sandbox->luaGlobalIntForTest("h"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("shapes"));
}

void test_get_by_name_and_absence(void) {
  main_ = new FakePanel(320, 172);
  RenderTargets::addPanel("main", main_);
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local m = surfaces.get('main')\n"
      "w, h = m.w, m.h\n"
      "missing = surfaces.get('nosuch') == nil\n"));
  TEST_ASSERT_EQUAL_INT(320, sandbox->luaGlobalIntForTest("w"));
  TEST_ASSERT_EQUAL_INT(172, sandbox->luaGlobalIntForTest("h"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("missing"));
}

// The reason the read exists: a board that registers its panel before the
// display driver's begin() caches zeros, because the panel does not know its
// own size yet. Reading must give the hardware's answer, not that cache.
void test_geometry_is_read_from_the_panel_not_the_cache(void) {
  main_ = new FakePanel(0, 0);        // pre-begin: the panel knows nothing
  RenderTargets::addPanel("main", main_);
  main_->resize(466, 466);            // the driver's begin() brings it up
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local m = surfaces.get('main')\n"
      "w, h = m.w, m.h\n"));
  TEST_ASSERT_EQUAL_INT(466, sandbox->luaGlobalIntForTest("w"));
  TEST_ASSERT_EQUAL_INT(466, sandbox->luaGlobalIntForTest("h"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lists_nothing_on_a_screenless_board);
  RUN_TEST(test_lists_registered_panels_in_order);
  RUN_TEST(test_get_by_name_and_absence);
  RUN_TEST(test_geometry_is_read_from_the_panel_not_the_cache);
  return UNITY_END();
}
