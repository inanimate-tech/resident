// lgfx module: lgfx.bind + the core LovyanGFX drawing set from Lua.
//
// Test approach: LovyanGFX itself doesn't compile host-side, so the module
// binds the minimal LgfxTarget interface and firmware uses the duck-typed
// LgfxLovyanTarget<TGfx> adapter. Here TGfx is FakeGfx — a recorder with
// LovyanGFX-shaped method signatures and a small pixel buffer — instantiated
// THROUGH the real adapter template, so the adapter's forwarding (including
// the setTextColor one/two-arg split and the presenter) is what's under test.
//
// R19 (bind is the claim) is exercised in situ through the third display,
// "panel": a module-owned LgfxSpriteTarget over a registered PanelTarget, so
// the claim on bind, the blit present path, and the stand-down of a
// non-owner's flip are tested as Lua actually reaches them.
#include <unity.h>
#include <string>
#include <vector>

#include "ResidentSandbox.cpp"
#include "ResidentLgfxModule.h"

namespace {

struct FakeGfx {
  static constexpr int32_t W = 16, H = 16;
  uint32_t px[H][W] = {};
  std::vector<std::string> calls;
  char buf[128];

  void log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    calls.push_back(buf);
  }
  void set(int32_t x, int32_t y, uint32_t c) {
    if (x >= 0 && x < W && y >= 0 && y < H) px[y][x] = c;
  }

  // LovyanGFX-shaped surface (what the adapter forwards to):
  void fillScreen(uint32_t c) {
    for (int32_t y = 0; y < H; y++)
      for (int32_t x = 0; x < W; x++) px[y][x] = c;
    log("fillScreen(%06x)", (unsigned)c);
  }
  void drawPixel(int32_t x, int32_t y, uint32_t c) {
    set(x, y, c);
    log("drawPixel(%d,%d,%06x)", (int)x, (int)y, (unsigned)c);
  }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
    log("drawLine(%d,%d,%d,%d,%06x)", (int)x0, (int)y0, (int)x1, (int)y1, (unsigned)c);
  }
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    log("drawRect(%d,%d,%d,%d,%06x)", (int)x, (int)y, (int)w, (int)h, (unsigned)c);
  }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
    for (int32_t yy = y; yy < y + h; yy++)
      for (int32_t xx = x; xx < x + w; xx++) set(xx, yy, c);
    log("fillRect(%d,%d,%d,%d,%06x)", (int)x, (int)y, (int)w, (int)h, (unsigned)c);
  }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
    log("drawRoundRect(%d,%d,%d,%d,%d,%06x)", (int)x, (int)y, (int)w, (int)h, (int)r, (unsigned)c);
  }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
    log("fillRoundRect(%d,%d,%d,%d,%d,%06x)", (int)x, (int)y, (int)w, (int)h, (int)r, (unsigned)c);
  }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
    log("drawCircle(%d,%d,%d,%06x)", (int)x, (int)y, (int)r, (unsigned)c);
  }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) {
    log("fillCircle(%d,%d,%d,%06x)", (int)x, (int)y, (int)r, (unsigned)c);
  }
  void drawTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint32_t c) {
    log("drawTriangle(%d,%d,%d,%d,%d,%d,%06x)",
        (int)x0, (int)y0, (int)x1, (int)y1, (int)x2, (int)y2, (unsigned)c);
  }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, uint32_t c) {
    log("fillTriangle(%d,%d,%d,%d,%d,%d,%06x)",
        (int)x0, (int)y0, (int)x1, (int)y1, (int)x2, (int)y2, (unsigned)c);
  }
  void setTextColor(uint32_t fg) { log("setTextColor(%06x)", (unsigned)fg); }
  void setTextColor(uint32_t fg, uint32_t bg) {
    log("setTextColor(%06x,%06x)", (unsigned)fg, (unsigned)bg);
  }
  void setTextSize(float s) { log("setTextSize(%.1f)", (double)s); }
  void setTextDatum(uint8_t d) { log("setTextDatum(%d)", (int)d); }
  void setCursor(int32_t x, int32_t y) { log("setCursor(%d,%d)", (int)x, (int)y); }
  void print(const char* t) { log("print(%s)", t); }
  void drawString(const char* t, int32_t x, int32_t y) {
    log("drawString(%s,%d,%d)", t, (int)x, (int)y);
  }
  int32_t width() { return W; }
  int32_t height() { return H; }

  // Sprite-shaped surface (what LgfxSpriteTarget manages):
  uint16_t frame[H * W] = {};
  bool created = false;
  int depth = 0;
  void setColorDepth(int d) { depth = d; }
  void* createSprite(int32_t, int32_t) { created = true; return frame; }
  void* getBuffer() { return created ? (void*)frame : nullptr; }
};

class FakePanel : public Resident::PanelTarget {
public:
  int32_t width() const override { return FakeGfx::W; }
  int32_t height() const override { return FakeGfx::H; }
  void blit(int32_t x, int32_t y, int32_t w, int32_t h,
            const uint16_t* px) override {
    blits++;
    lastX = x; lastY = y; lastW = w; lastH = h; lastPx = px;
  }
  int blits = 0;
  int32_t lastX = -1, lastY = -1, lastW = -1, lastH = -1;
  const uint16_t* lastPx = nullptr;
};

FakeGfx* gfxMain = nullptr;
FakeGfx* gfxAux = nullptr;
FakeGfx* gfxSprite = nullptr;
FakePanel* fakePanel = nullptr;
int flips = 0;
Resident::LgfxLovyanTarget<FakeGfx>* targetMain = nullptr;
Resident::LgfxLovyanTarget<FakeGfx>* targetAux = nullptr;
Resident::LgfxSpriteTarget<FakeGfx>* targetPanel = nullptr;
Resident::LgfxModule* lgfxModule = nullptr;
Resident::Sandbox* sandbox = nullptr;

constexpr const char* IDLE_APP =
    "function init(ctx) end\n"
    "function on_tick(ctx, dt) end\n";

void build() {
  Resident::RenderTargets::clear();
  gfxMain = new FakeGfx();
  gfxAux = new FakeGfx();
  gfxSprite = new FakeGfx();
  fakePanel = new FakePanel();
  flips = 0;
  targetMain = new Resident::LgfxLovyanTarget<FakeGfx>(gfxMain, [] { flips++; });
  targetAux = new Resident::LgfxLovyanTarget<FakeGfx>(gfxAux);  // no presenter
  targetPanel = new Resident::LgfxSpriteTarget<FakeGfx>(gfxSprite);
  Resident::RenderTargets::addPanel("panel", fakePanel);
  lgfxModule = new Resident::LgfxModule();
  lgfxModule->addDisplay("main", targetMain);
  lgfxModule->addDisplay("aux", targetAux);
  lgfxModule->addDisplay("panel", targetPanel);

  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {lgfxModule};
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();

  JsonDocument doc;
  doc["channel"] = "system";
  doc["type"] = "app";
  doc["code"] = IDLE_APP;
  sandbox->injectMessage("test", "app", doc);
}

bool logged(FakeGfx* g, const char* call) {
  for (auto& c : g->calls)
    if (c == call) return true;
  return false;
}
}  // namespace

void setUp(void) { testMillis() = 0; }

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete lgfxModule; lgfxModule = nullptr;
  delete targetMain; targetMain = nullptr;
  delete targetAux; targetAux = nullptr;
  delete targetPanel; targetPanel = nullptr;
  delete gfxMain; gfxMain = nullptr;
  delete gfxAux; gfxAux = nullptr;
  delete gfxSprite; gfxSprite = nullptr;
  delete fakePanel; fakePanel = nullptr;
  Resident::RenderTargets::clear();
}

void test_bind_dimensions_pixels_and_flip(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local g = lgfx.bind('main')\n"
      "w, h = g:width(), g:height()\n"
      "g:fillScreen(0x112233)\n"
      "g:drawPixel(2, 3, 0x00FF00)\n"
      "g:flip()\n"));
  TEST_ASSERT_EQUAL_INT(16, sandbox->luaGlobalIntForTest("w"));
  TEST_ASSERT_EQUAL_INT(16, sandbox->luaGlobalIntForTest("h"));
  TEST_ASSERT_EQUAL_UINT32(0x112233u, gfxMain->px[0][0]);
  TEST_ASSERT_EQUAL_UINT32(0x00FF00u, gfxMain->px[3][2]);   // [y][x]
  TEST_ASSERT_EQUAL_INT(1, flips);
}

void test_fillrect_pixels_inside_and_outside(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local g = lgfx.bind('main')\n"
      "g:fillScreen(0x000000)\n"
      "g:fillRect(1, 1, 4, 4, 0xFF0000)\n"));
  TEST_ASSERT_EQUAL_UINT32(0xFF0000u, gfxMain->px[1][1]);
  TEST_ASSERT_EQUAL_UINT32(0xFF0000u, gfxMain->px[4][4]);
  TEST_ASSERT_EQUAL_UINT32(0x000000u, gfxMain->px[5][5]);   // exclusive edge
  TEST_ASSERT_EQUAL_UINT32(0x000000u, gfxMain->px[0][0]);
}

void test_all_primitives_forwarded_with_args(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local g = lgfx.bind('main')\n"
      "g:drawLine(0, 1, 10, 11, 0x102030)\n"
      "g:drawRect(1, 2, 3, 4, 0x405060)\n"
      "g:drawRoundRect(1, 2, 10, 8, 3, 0x0000FF)\n"
      "g:fillRoundRect(2, 3, 11, 9, 4, 0x00FF00)\n"
      "g:drawCircle(8, 8, 5, 0xABCDEF)\n"
      "g:fillCircle(7, 6, 4, 0x123456)\n"
      "g:drawTriangle(0, 0, 5, 0, 2, 4, 0x111111)\n"
      "g:fillTriangle(1, 1, 6, 1, 3, 5, 0x222222)\n"));
  TEST_ASSERT_TRUE(logged(gfxMain, "drawLine(0,1,10,11,102030)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "drawRect(1,2,3,4,405060)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "drawRoundRect(1,2,10,8,3,0000ff)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "fillRoundRect(2,3,11,9,4,00ff00)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "drawCircle(8,8,5,abcdef)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "fillCircle(7,6,4,123456)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "drawTriangle(0,0,5,0,2,4,111111)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "fillTriangle(1,1,6,1,3,5,222222)"));
}

void test_text_calls_and_datum_constants(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local g = lgfx.bind('main')\n"
      "g:setTextColor(0xFFFFFF)\n"
      "g:setTextColor(0xFF0000, 0x000000)\n"
      "g:setTextSize(2)\n"
      "g:setTextDatum(lgfx.MC_DATUM)\n"
      "g:setCursor(4, 5)\n"
      "g:print('hi')\n"
      "g:drawString('yo', 8, 9)\n"));
  TEST_ASSERT_TRUE(logged(gfxMain, "setTextColor(ffffff)"));           // 1-arg path
  TEST_ASSERT_TRUE(logged(gfxMain, "setTextColor(ff0000,000000)"));    // 2-arg path
  TEST_ASSERT_TRUE(logged(gfxMain, "setTextSize(2.0)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "setTextDatum(5)"));                // MC = 5
  TEST_ASSERT_TRUE(logged(gfxMain, "setCursor(4,5)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "print(hi)"));
  TEST_ASSERT_TRUE(logged(gfxMain, "drawString(yo,8,9)"));
}

void test_two_displays_bind_independently(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "local a = lgfx.bind('main')\n"
      "local b = lgfx.bind('aux')\n"
      "a:drawPixel(0, 0, 0x111111)\n"
      "b:drawPixel(0, 0, 0x222222)\n"
      "b:flip()\n"));   // aux has no presenter: must be a safe no-op
  TEST_ASSERT_EQUAL_UINT32(0x111111u, gfxMain->px[0][0]);
  TEST_ASSERT_EQUAL_UINT32(0x222222u, gfxAux->px[0][0]);
  TEST_ASSERT_EQUAL_INT(0, flips);
}

void test_bind_unknown_display_raises(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "ok, err = pcall(function() return lgfx.bind('nope') end)\n"
      "failed = (ok == false) and (err:find('nope') ~= nil)\n"));
  TEST_ASSERT_TRUE(sandbox->luaGlobalBoolForTest("failed"));
}

// ── R19: bind is the claim ───────────────────────────────────────────────

void test_sprite_bind_allocates_claims_and_blits(void) {
  build();
  TEST_ASSERT_FALSE(gfxSprite->created);
  TEST_ASSERT_TRUE(sandbox->loadChunk(
      "g = lgfx.bind('panel')\n"
      "g:fillScreen(0x102030)\n"
      "g:flip()\n"));
  TEST_ASSERT_TRUE(gfxSprite->created);            // module owns the buffer
  TEST_ASSERT_EQUAL_INT(16, gfxSprite->depth);     // 16bpp before create
  TEST_ASSERT_EQUAL_UINT8(Resident::RenderTargets::MODULE_LGFX,
                          Resident::RenderTargets::owner("panel"));
  TEST_ASSERT_EQUAL_INT(1, fakePanel->blits);      // one whole-frame blit
  TEST_ASSERT_EQUAL_INT(0, (int)fakePanel->lastX);
  TEST_ASSERT_EQUAL_INT(0, (int)fakePanel->lastY);
  TEST_ASSERT_EQUAL_INT(FakeGfx::W, (int)fakePanel->lastW);
  TEST_ASSERT_EQUAL_INT(FakeGfx::H, (int)fakePanel->lastH);
  TEST_ASSERT_EQUAL_PTR(gfxSprite->frame, fakePanel->lastPx);
}

void test_flip_stands_down_when_the_other_library_claims(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk("g = lgfx.bind('panel')\ng:flip()\n"));
  TEST_ASSERT_EQUAL_INT(1, fakePanel->blits);

  // lvgl.bind('panel') on the same target — the walkie-talkie rule.
  Resident::RenderTargets::claim("panel", Resident::RenderTargets::MODULE_LVGL);
  TEST_ASSERT_TRUE(sandbox->loadChunk("g:flip()\ng:flip()\n"));
  TEST_ASSERT_EQUAL_INT(1, fakePanel->blits);      // dropped silently

  // Drawing still works — only the present path stands down.
  TEST_ASSERT_TRUE(sandbox->loadChunk("g:drawPixel(1, 2, 0x00FF00)\n"));
  TEST_ASSERT_EQUAL_UINT32(0x00FF00u, gfxSprite->px[2][1]);
}

void test_app_reset_releases_and_the_next_bind_reclaims(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk("g = lgfx.bind('panel')\ng:flip()\n"));
  TEST_ASSERT_EQUAL_INT(1, fakePanel->blits);

  lgfxModule->onAppReset();
  TEST_ASSERT_EQUAL_UINT8(0, Resident::RenderTargets::owner("panel"));
  TEST_ASSERT_TRUE(sandbox->loadChunk("g:flip()\n"));
  TEST_ASSERT_EQUAL_INT(1, fakePanel->blits);      // unowned: nothing presents

  TEST_ASSERT_TRUE(sandbox->loadChunk("g = lgfx.bind('panel')\ng:flip()\n"));
  TEST_ASSERT_EQUAL_INT(2, fakePanel->blits);      // claimed back
}

void test_legacy_presenter_target_also_obeys_ownership(void) {
  build();
  TEST_ASSERT_TRUE(sandbox->loadChunk("m = lgfx.bind('main')\nm:flip()\n"));
  TEST_ASSERT_EQUAL_INT(1, flips);                 // presenter ran
  Resident::RenderTargets::claim("main", Resident::RenderTargets::MODULE_LVGL);
  TEST_ASSERT_TRUE(sandbox->loadChunk("m:flip()\n"));
  TEST_ASSERT_EQUAL_INT(1, flips);                 // stood down
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bind_dimensions_pixels_and_flip);
  RUN_TEST(test_fillrect_pixels_inside_and_outside);
  RUN_TEST(test_all_primitives_forwarded_with_args);
  RUN_TEST(test_text_calls_and_datum_constants);
  RUN_TEST(test_two_displays_bind_independently);
  RUN_TEST(test_bind_unknown_display_raises);
  RUN_TEST(test_sprite_bind_allocates_claims_and_blits);
  RUN_TEST(test_flip_stands_down_when_the_other_library_claims);
  RUN_TEST(test_app_reset_releases_and_the_next_bind_reclaims);
  RUN_TEST(test_legacy_presenter_target_also_obeys_ownership);
  return UNITY_END();
}
