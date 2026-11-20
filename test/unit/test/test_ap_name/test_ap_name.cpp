// The WiFi setup portal's network name: SandboxConfig::apName, its default,
// the 32-byte limit, and the name a networked sandbox gives Courier.
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
Resident::Sandbox* sandbox = nullptr;
}

void setUp(void) {}
void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
}

void test_defaults_to_resident_and_capitalised_type(void) {
  TEST_ASSERT_EQUAL_STRING(
      "Resident Clock 3b21",
      Resident::Sandbox::buildAPName(nullptr, "clock", "3b21aa55").c_str());
}

void test_empty_name_takes_the_default(void) {
  TEST_ASSERT_EQUAL_STRING(
      "Resident Clock 3b21",
      Resident::Sandbox::buildAPName("", "clock", "3b21aa55").c_str());
}

void test_no_type_is_resident_alone(void) {
  TEST_ASSERT_EQUAL_STRING(
      "Resident 3b21",
      Resident::Sandbox::buildAPName(nullptr, "", "3b21aa55").c_str());
}

void test_name_replaces_the_default(void) {
  TEST_ASSERT_EQUAL_STRING(
      "Acme Chrono 3b21",
      Resident::Sandbox::buildAPName("Acme Chrono", "clock", "3b21aa55").c_str());
}

void test_long_name_is_shortened_to_32_bytes_keeping_the_id(void) {
  String name = Resident::Sandbox::buildAPName(
      "An Extremely Long Product Name That Goes On", "clock", "3b21aa55");
  TEST_ASSERT_EQUAL(32, (int)name.length());
  TEST_ASSERT_EQUAL_STRING("An Extremely Long Product N 3b21", name.c_str());
}

void test_shortening_never_cuts_a_utf8_character(void) {
  // 26 ASCII bytes then "é" (2 bytes): byte 27 is the middle of it.
  String name = Resident::Sandbox::buildAPName(
      "abcdefghijklmnopqrstuvwxyz\xC3\xA9tail", "clock", "3b21aa55");
  TEST_ASSERT_TRUE(name.length() <= 32);
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwxyz 3b21", name.c_str());
}

void test_setup_names_the_portal_from_config(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "clock";
  cfg.apName = "Acme Chrono";
  Courier::Config courier;
  courier.host = "example.com";
  cfg.network = courier;

  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();

  String expected = String("Acme Chrono ") + sandbox->getDeviceId().substring(0, 4);
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), sandbox->getAPName().c_str());
}

void test_setup_without_a_name_keeps_the_resident_default(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "clock";
  Courier::Config courier;
  courier.host = "example.com";
  cfg.network = courier;

  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();

  String expected = String("Resident Clock ") + sandbox->getDeviceId().substring(0, 4);
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), sandbox->getAPName().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_resident_and_capitalised_type);
  RUN_TEST(test_empty_name_takes_the_default);
  RUN_TEST(test_no_type_is_resident_alone);
  RUN_TEST(test_name_replaces_the_default);
  RUN_TEST(test_long_name_is_shortened_to_32_bytes_keeping_the_id);
  RUN_TEST(test_shortening_never_cuts_a_utf8_character);
  RUN_TEST(test_setup_names_the_portal_from_config);
  RUN_TEST(test_setup_without_a_name_keeps_the_resident_default);
  return UNITY_END();
}
