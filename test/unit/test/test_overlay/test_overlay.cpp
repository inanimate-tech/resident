// Overlay arbiter: claims are resolved PER SURFACE (highest priority wins,
// ties to earlier registration; distinct surfaces are independent). A
// dual-role surface (in extensions[] AND systemDisplay) suspends the app
// while claimed; the arbiter calls SystemDisplay::restoreContent() when a
// surface's last claim releases. onDraw is paced on the app-tick cadence.
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
class SpyDisplay : public Resident::SystemDisplay {
public:
  int restores = 0;
  const char* name() const override { return "display"; }
  void update() override {}
  void displayText(const char* t) override { (void)t; }
  void restoreContent() override { restores++; }
};

class FakeOverlay : public Resident::Overlay {
public:
  int acquires = 0, releases = 0, draws = 0;
  unsigned long lastDt = 0;
  void onAcquire() override { acquires++; }
  void onDraw(unsigned long dt) override { draws++; lastDt = dt; }
  void onRelease() override { releases++; }
};

constexpr const char* APP =
    "function init(ctx) end\nfunction on_tick(ctx, dt) end\n";

SpyDisplay* disp = nullptr;
SpyDisplay* disp2 = nullptr;
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

void tearDown(void) {
  delete sandbox; sandbox = nullptr;
  delete disp; disp = nullptr;
  delete disp2; disp2 = nullptr;
}
void setUp(void) { testMillis() = 0; }

void test_dual_role_overlay_suspends_and_restores_surface(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay ov;
  sandbox->addOverlay(&ov, disp, 100);

  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, ov.acquires);
  TEST_ASSERT_EQUAL_INT(1, ov.draws);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->requestOverlay(&ov, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, ov.releases);
  TEST_ASSERT_EQUAL_INT(1, disp->restores);      // surface restores, not overlay
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
}

void test_priority_arbitration(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay hi, lo;
  sandbox->addOverlay(&hi, disp, 100);
  sandbox->addOverlay(&lo, disp, 50);

  sandbox->requestOverlay(&hi, true);
  sandbox->requestOverlay(&lo, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, hi.acquires);    // higher wins
  TEST_ASSERT_EQUAL_INT(0, lo.acquires);

  sandbox->requestOverlay(&hi, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, lo.acquires);    // lower takes over
  TEST_ASSERT_EQUAL_INT(0, disp->restores); // handoff: surface never released
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

  FakeOverlay ov;
  sandbox->addOverlay(&ov, disp, 100);
  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, ov.draws);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());  // app keeps running
}

void test_null_surface_is_independent(void) {
  // Two claims on DIFFERENT surfaces (one null = dedicated) are independent:
  // both win concurrently regardless of priority.
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay onShared, onDedicated;
  sandbox->addOverlay(&onShared, disp, 100);
  sandbox->addOverlay(&onDedicated, nullptr, 1);   // lower priority, own surface

  sandbox->requestOverlay(&onShared, true);
  sandbox->requestOverlay(&onDedicated, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, onShared.acquires);
  TEST_ASSERT_EQUAL_INT(1, onDedicated.acquires);  // not outranked — no contest
  TEST_ASSERT_EQUAL_INT(1, onShared.draws);
  TEST_ASSERT_EQUAL_INT(1, onDedicated.draws);

  // Releasing the shared claim doesn't touch the dedicated one.
  sandbox->requestOverlay(&onShared, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, onShared.releases);
  TEST_ASSERT_EQUAL_INT(0, onDedicated.releases);
  TEST_ASSERT_EQUAL_INT(1, disp->restores);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
}

void test_two_surfaces_arbitrate_independently(void) {
  // time-p1 shape: status overlay on the (role-only) system display, voice
  // overlay on its own dedicated surface — both draw at once.
  disp = new SpyDisplay();
  disp2 = new SpyDisplay();
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemDisplay = disp;
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();
  sandbox->loadApp(APP);

  FakeOverlay voice, status;
  sandbox->addOverlay(&voice, disp2, 100);
  sandbox->addOverlay(&status, disp, 50);

  sandbox->requestOverlay(&voice, true);
  sandbox->requestOverlay(&status, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, voice.acquires);
  TEST_ASSERT_EQUAL_INT(1, status.acquires);   // different surface — no contest

  sandbox->requestOverlay(&voice, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, disp2->restores);
  TEST_ASSERT_EQUAL_INT(0, disp->restores);
  TEST_ASSERT_EQUAL_INT(0, status.releases);   // untouched by voice releasing
}

void test_remove_active_overlay_resumes_app(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay ov;
  sandbox->addOverlay(&ov, disp, 100);

  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->removeOverlay(&ov);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
  TEST_ASSERT_EQUAL_INT(1, ov.releases);
  TEST_ASSERT_EQUAL_INT(1, disp->restores);

  runLoop(1);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());  // no relapse
}

void test_swap_between_dual_role_overlays_keeps_suspended_no_restore(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay hi, lo;
  sandbox->addOverlay(&hi, disp, 100);
  sandbox->addOverlay(&lo, disp, 50);

  sandbox->requestOverlay(&hi, true);
  sandbox->requestOverlay(&lo, true);
  runLoop(1);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->requestOverlay(&hi, false);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, lo.acquires);
  TEST_ASSERT_EQUAL_INT(1, hi.releases);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());   // handoff to lo, still suspended
  TEST_ASSERT_EQUAL_INT(0, disp->restores);      // no restore during handoff
}

void test_app_loaded_under_claim_starts_suspended(void) {
  buildDualRole();
  FakeOverlay ov;
  sandbox->addOverlay(&ov, disp, 100);
  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_EQUAL_INT(1, ov.acquires);

  // App arrives while the claim is held: it must not tick under the overlay.
  sandbox->loadApp(APP);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->requestOverlay(&ov, false);
  runLoop(1);
  TEST_ASSERT_FALSE(sandbox->isAppSuspended());
}

void test_device_suspension_survives_overlay_cycle(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  sandbox->suspendApp();                     // device-initiated
  FakeOverlay ov;
  sandbox->addOverlay(&ov, disp, 100);

  sandbox->requestOverlay(&ov, true);
  runLoop(1);
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());

  sandbox->requestOverlay(&ov, false);
  runLoop(1);
  // The arbiter didn't suspend, so it must not resume.
  TEST_ASSERT_TRUE(sandbox->isAppSuspended());
}

void test_draw_paced_on_tick_cadence(void) {
  buildDualRole();
  sandbox->loadApp(APP);
  FakeOverlay ov;
  sandbox->addOverlay(&ov, disp, 100);
  sandbox->requestOverlay(&ov, true);

  // Loop steps of 20ms: with TICK_INTERVAL=100 only every 5th loop draws.
  int drawsBefore = ov.draws;
  for (int i = 0; i < 10; i++) { testMillis() += 20; sandbox->loop(); }
  int drawn = ov.draws - drawsBefore;
  TEST_ASSERT_TRUE(drawn >= 2 && drawn <= 3);   // ~200ms of 100ms cadence
  TEST_ASSERT_TRUE(ov.lastDt >= 100);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_dual_role_overlay_suspends_and_restores_surface);
  RUN_TEST(test_priority_arbitration);
  RUN_TEST(test_dedicated_surface_does_not_suspend);
  RUN_TEST(test_null_surface_is_independent);
  RUN_TEST(test_two_surfaces_arbitrate_independently);
  RUN_TEST(test_remove_active_overlay_resumes_app);
  RUN_TEST(test_swap_between_dual_role_overlays_keeps_suspended_no_restore);
  RUN_TEST(test_app_loaded_under_claim_starts_suspended);
  RUN_TEST(test_device_suspension_survives_overlay_cycle);
  RUN_TEST(test_draw_paced_on_tick_cadence);
  UNITY_END();
  return 0;
}
