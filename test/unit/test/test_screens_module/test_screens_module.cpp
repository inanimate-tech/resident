// Display drivers and the `screens` module.
//
// A board lists a DisplayDriver in cfg.extensions and its screens exist: the
// sandbox registers each one at initialize, so drawing libraries find them by
// name with no addPanel, and Lua reads their facts and sets their knobs
// through `screens`. One driver may own several screens.
#include <unity.h>

#include "ResidentSandbox.cpp"
#include "ResidentRenderTargets.h"
#include "ResidentDisplayDriver.h"

namespace {

using Resident::RenderTargets;

class FakePanel : public Resident::PanelTarget {
public:
  FakePanel(int32_t w, int32_t h) : _w(w), _h(h) {}
  int32_t width() const override { return _w; }
  int32_t height() const override { return _h; }
  void blit(int32_t, int32_t, int32_t, int32_t, const uint16_t*) override {}
  void frameDone() override { frames++; }
  int frames = 0;
private:
  int32_t _w, _h;
};

// Two key caps on one backlight rail, the second one-bit, with a brightness
// knob, an e-paper-style refresh on the second, and a reset on app load.
class TwoKeys : public Resident::DisplayDriver {
public:
  FakePanel a{128, 128}, b{128, 64};
  float brightness = 0.8f;
  int refreshed = -1;
  const char* name() const override { return "keys"; }
  int screenCount() const override { return 2; }
  Resident::Screen screen(int i) const override {
    Resident::Screen s;
    s.name = i == 0 ? "key1" : "key2";
    s.target = const_cast<FakePanel*>(i == 0 ? &a : &b);
    s.shape = i == 0 ? "round" : "rect";
    s.depth = i == 0 ? 16 : 1;
    s.dpi = 213;
    s.group = 1;
    return s;
  }
  bool setScreen(int, const char* key, lua_State* L, int idx) override {
    if (strcmp(key, "brightness") != 0) return false;
    brightness = (float)luaL_checknumber(L, idx);
    return true;
  }
  void getScreen(int i, lua_State* L) override {
    lua_pushnumber(L, brightness);
    lua_setfield(L, -2, "brightness");
    if (i == 1) { lua_pushboolean(L, false); lua_setfield(L, -2, "busy"); }
  }
  bool refresh(int i) override {
    if (i != 1) return false;
    refreshed = i;
    return true;
  }
  void onAppReset() override { brightness = 0.8f; }
};

Resident::Sandbox* sandbox = nullptr;
TwoKeys* keys = nullptr;
FakePanel* bare = nullptr;

void build(bool withDriver = true) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  if (withDriver) cfg.extensions = {keys};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = "function on_tick(ctx, dt) end\n";
  sandbox->injectMessage("test", "app", doc);
}

bool lua(const char* chunk) { return sandbox->loadChunk(chunk); }
bool flag(const char* n) { return sandbox->luaGlobalBoolForTest(n); }
int num(const char* n) { return sandbox->luaGlobalIntForTest(n); }

}  // namespace

void setUp(void) {
  testMillis() = 0;
  RenderTargets::clear();
  keys = new TwoKeys();
  bare = nullptr;
}

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete keys; keys = nullptr;
  delete bare; bare = nullptr;
  RenderTargets::clear();
}

void test_a_listed_display_driver_registers_its_screens(void) {
  build();
  TEST_ASSERT_EQUAL_INT(2, RenderTargets::count());
  TEST_ASSERT_TRUE(RenderTargets::panel("key1") == &keys->a);
  TEST_ASSERT_TRUE(RenderTargets::panel("key2") == &keys->b);
  const RenderTargets::Entry& e = RenderTargets::entry(RenderTargets::indexOf("key2"));
  TEST_ASSERT_TRUE(e.driver == keys);
  TEST_ASSERT_EQUAL_INT(1, e.screenIndex);
  TEST_ASSERT_EQUAL_INT(1, e.depth);
  TEST_ASSERT_EQUAL_INT(213, e.dpi);
}

void test_an_unlisted_driver_registers_nothing(void) {
  build(false);
  TEST_ASSERT_EQUAL_INT(0, RenderTargets::count());
  TEST_ASSERT_TRUE(lua("n = #screens.list()\n"));
  TEST_ASSERT_EQUAL_INT(0, num("n"));
}

void test_list_carries_each_screens_facts(void) {
  build();
  TEST_ASSERT_TRUE(lua(
      "local s = screens.list()\n"
      "n = #s\n"
      "ok = s[1].name == 'key1' and s[1].w == 128 and s[1].h == 128 and s[1].shape == 'round'\n"
      "  and s[1].depth == 16 and s[1].dpi == 213 and s[1].group == 1\n"
      "  and s[2].name == 'key2' and s[2].h == 64 and s[2].depth == 1\n"));
  TEST_ASSERT_EQUAL_INT(2, num("n"));
  TEST_ASSERT_TRUE(flag("ok"));
}

void test_get_adds_the_drivers_settings_and_status(void) {
  build();
  TEST_ASSERT_TRUE(lua(
      "local s = screens.get('key2')\n"
      "ok = math.abs(s.brightness - 0.8) < 1e-3 and s.busy == false and s.name == 'key2'\n"
      "none = screens.get('key9') == nil\n"));
  TEST_ASSERT_TRUE(flag("ok"));
  TEST_ASSERT_TRUE(flag("none"));
}

void test_set_reaches_the_driver_and_unknown_keys_raise(void) {
  build();
  TEST_ASSERT_TRUE(lua("screens.set('key1', { brightness = 0.25 })\n"));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.25f, keys->brightness);
  TEST_ASSERT_TRUE(lua(
      "local ok, err = pcall(screens.set, 'key1', { sparkle = 1 })\n"
      "raised = not ok and tostring(err):find(\"no setting 'sparkle'\") ~= nil\n"
      "local ok2, err2 = pcall(screens.set, 'nope', { brightness = 1 })\n"
      "unknown = not ok2 and tostring(err2):find(\"no screen named 'nope'\") ~= nil\n"));
  TEST_ASSERT_TRUE(flag("raised"));
  TEST_ASSERT_TRUE(flag("unknown"));
}

void test_refresh_is_the_drivers_answer(void) {
  build();
  TEST_ASSERT_TRUE(lua("a = screens.refresh('key1')\nb = screens.refresh('key2')\n"));
  TEST_ASSERT_FALSE(flag("a"));
  TEST_ASSERT_TRUE(flag("b"));
  TEST_ASSERT_EQUAL_INT(1, keys->refreshed);
}

void test_settings_reset_with_the_app(void) {
  build();
  TEST_ASSERT_TRUE(lua("screens.set('key1', { brightness = 0.1 })\n"));
  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = "function on_tick(ctx, dt) end\n";
  sandbox->injectMessage("test", "app", doc);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.8f, keys->brightness);
}

void test_a_hand_registered_panel_is_a_screen_without_settings(void) {
  bare = new FakePanel(64, 32);
  RenderTargets::addPanel("main", bare);
  build(false);
  TEST_ASSERT_TRUE(lua(
      "local s = screens.get('main')\n"
      "listed = s ~= nil and s.w == 64 and s.depth == 16\n"
      "local ok, err = pcall(screens.set, 'main', { brightness = 1 })\n"
      "raised = not ok\n"
      "no_refresh = screens.refresh('main') == false\n"));
  TEST_ASSERT_TRUE(flag("listed"));
  TEST_ASSERT_TRUE(flag("raised"));
  TEST_ASSERT_TRUE(flag("no_refresh"));
}

void test_surfaces_still_lists_them(void) {
  build();
  TEST_ASSERT_TRUE(lua("n = #surfaces.list()\n"));
  TEST_ASSERT_EQUAL_INT(2, num("n"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_listed_display_driver_registers_its_screens);
  RUN_TEST(test_an_unlisted_driver_registers_nothing);
  RUN_TEST(test_list_carries_each_screens_facts);
  RUN_TEST(test_get_adds_the_drivers_settings_and_status);
  RUN_TEST(test_set_reaches_the_driver_and_unknown_keys_raise);
  RUN_TEST(test_refresh_is_the_drivers_answer);
  RUN_TEST(test_settings_reset_with_the_app);
  RUN_TEST(test_a_hand_registered_panel_is_a_screen_without_settings);
  RUN_TEST(test_surfaces_still_lists_them);
  return UNITY_END();
}
