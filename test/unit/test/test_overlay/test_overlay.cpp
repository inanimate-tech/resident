// Overlay arbiter: highest-priority requested overlay draws; a dual-role
// surface (in extensions[] AND systemDisplay) suspends the app; a dedicated
// surface does not.
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
class SpyDisplay : public Resident::SystemDisplay {
public:
  const char* name() const override { return "display"; }
  void update() override {}
  void displayText(const char* t) override { (void)t; }
};

class FakeOverlay : public Resident::Overlay {
public:
  int prio;
  int activates = 0, deactivates = 0, draws = 0, restores = 0;
  explicit FakeOverlay(int p) : prio(p) {}
  int priority() const override { return prio; }
  void onActivate() override { activates++; }
  void onDraw() override { draws++; }
  void onDeactivate() override { deactivates++; }
  void restore() override { restores++; }
};

constexpr const char* APP =
    "function init(ctx) end\nfunction on_tick(ctx, dt) end\n";

SpyDisplay* disp = nullptr;
Resident::Sandbox* sandbox = nullptr;

void runLoop(int n) { for (int i = 0; i < n; i++) { testMillis() += 200; sandbox->loop(); } }

// Build a sandbox whose display is dual-role (app screen AND systemDisplay).
void buildDualRole() {
  disp = new SpyDisplay();
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.extensions = {disp};        // app-facing …
  cfg.systemDisplay = disp;       // … and the system display → appDrawsTo == true
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
}
}  // namespace

void tearDown(void) { delete sandbox; sandbox = nullptr; delete disp; disp = nullptr; }
void setUp(void) { testMillis() = 0; }

void test_dual_role_overlay_suspends_and_restores(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay ov(100);
  sandbox->addOverlay(&ov, disp);

  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, ov.activates);
  TEST_ASSERT_EQUAL_INT(1, ov.draws);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->requestOverlay(&ov, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, ov.deactivates);
  TEST_ASSERT_EQUAL_INT(1, ov.restores);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
}

void test_priority_arbitration(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay hi(100), lo(50);
  sandbox->addOverlay(&hi, disp);
  sandbox->addOverlay(&lo, disp);

  sandbox->requestOverlay(&hi, true);
  sandbox->requestOverlay(&lo, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, hi.activates);   // higher wins
  TEST_ASSERT_EQUAL_INT(0, lo.activates);

  sandbox->requestOverlay(&hi, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, lo.activates);   // lower takes over
}

void test_dedicated_surface_does_not_suspend(void) {
  // Display is a system role slot but NOT an app extension → appDrawsTo false.
  disp = new SpyDisplay();
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemDisplay = disp;                 // role-only (dedicated)
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  sandbox->loadApp(APP);

  FakeOverlay ov(100);
  sandbox->addOverlay(&ov, disp);
  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, ov.draws);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());  // app keeps running
}

void test_remove_active_overlay_resumes_app(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay ov(100);
  sandbox->addOverlay(&ov, disp);

  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->removeOverlay(&ov);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
  TEST_ASSERT_EQUAL_INT(1, ov.deactivates);
  TEST_ASSERT_EQUAL_INT(1, ov.restores);

  runLoop(1);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());  // no relapse
}

void test_swap_between_dual_role_overlays_keeps_suspended_no_restore(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay hi(100), lo(50);
  sandbox->addOverlay(&hi, disp);
  sandbox->addOverlay(&lo, disp);

  sandbox->requestOverlay(&hi, true);
  sandbox->requestOverlay(&lo, true);
  runLoop(1);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->requestOverlay(&hi, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, lo.activates);
  TEST_ASSERT_EQUAL_INT(1, hi.deactivates);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());   // handoff to lo, still suspended
  TEST_ASSERT_EQUAL_INT(0, hi.restores);         // no restore during dual-role handoff
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dual_role_overlay_suspends_and_restores);
  RUN_TEST(test_priority_arbitration);
  RUN_TEST(test_dedicated_surface_does_not_suspend);
  RUN_TEST(test_remove_active_overlay_resumes_app);
  RUN_TEST(test_swap_between_dual_role_overlays_keeps_suspended_no_restore);
  UNITY_END();
  return 0;
}
