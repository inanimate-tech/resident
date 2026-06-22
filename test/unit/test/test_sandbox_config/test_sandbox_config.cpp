#include <unity.h>
#include <type_traits>
#include "ResidentSandboxConfig.h"
#include "ResidentExtension.h"

namespace {
class StubExt : public Resident::Extension {
public:
  const char* name() const override { return "stub"; }
};
class StubStatusDisplay : public Resident::StatusDisplay {
public:
  const char* name() const override { return "stub-display"; }
  void displayText(const char* text) override { (void)text; }
};
class StubStatusLED : public Resident::StatusLED {
public:
  const char* name() const override { return "stub-led"; }
  void solidColor(uint32_t rgb) override { (void)rgb; }
};
}

void setUp(void) {}
void tearDown(void) {}

void test_default_construction(void) {
    Resident::SandboxConfig cfg;
    TEST_ASSERT_NULL(cfg.deviceType);
    TEST_ASSERT_NULL(cfg.statusDisplay);
    TEST_ASSERT_NULL(cfg.statusLED);
    TEST_ASSERT_FALSE(cfg.network.has_value());
    TEST_ASSERT_EQUAL_INT(0, (int)cfg.extensions.count);
    TEST_ASSERT_NULL(cfg.shaderTemplate);
    TEST_ASSERT_NULL(cfg.timezone);
}

void test_assign_top_level_fields(void) {
    StubExt e;
    StubStatusDisplay sd;
    StubStatusLED sl;

    Resident::SandboxConfig cfg;
    cfg.deviceType    = "feather-tft";
    cfg.extensions    = {&e};
    cfg.statusDisplay = &sd;
    cfg.statusLED     = &sl;
    cfg.timezone      = "Europe/London";

    TEST_ASSERT_EQUAL_STRING("feather-tft", cfg.deviceType);
    TEST_ASSERT_EQUAL_INT(1, (int)cfg.extensions.count);
    TEST_ASSERT_EQUAL_PTR(&sd, cfg.statusDisplay);
    TEST_ASSERT_EQUAL_PTR(&sl, cfg.statusLED);
    TEST_ASSERT_EQUAL_STRING("Europe/London", cfg.timezone);
}

void test_network_optional_can_be_assigned(void) {
    Resident::SandboxConfig cfg;
    Courier::Config c;
    c.host = "resident.inanimate.tech";
    c.dns1 = 0x08080808;
    cfg.network = c;

    TEST_ASSERT_TRUE(cfg.network.has_value());
    TEST_ASSERT_EQUAL_STRING("resident.inanimate.tech", cfg.network->host);
    TEST_ASSERT_EQUAL_UINT32(0x08080808, cfg.network->dns1);
}

void test_persistence_config_defaults(void) {
  Resident::SandboxConfig cfg;
  TEST_ASSERT_TRUE(cfg.persistApps);                  // persistence on by default
  TEST_ASSERT_NULL(cfg.systemButton);                 // no system button by default
  TEST_ASSERT_NULL(cfg.persistentStore);              // default store selected by Sandbox
}

void test_roles_are_drivers(void) {
  // The role interfaces must be Drivers (hence Extensions) so lifecycle is
  // unified and a role pointer is an Extension pointer.
  TEST_ASSERT_TRUE((std::is_base_of<Resident::Driver, Resident::StatusDisplay>::value));
  TEST_ASSERT_TRUE((std::is_base_of<Resident::Driver, Resident::SystemButton>::value));
  TEST_ASSERT_TRUE((std::is_base_of<Resident::Driver, Resident::StatusLED>::value));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_default_construction);
    RUN_TEST(test_assign_top_level_fields);
    RUN_TEST(test_network_optional_can_be_assigned);
    RUN_TEST(test_persistence_config_defaults);
    RUN_TEST(test_roles_are_drivers);
    return UNITY_END();
}
