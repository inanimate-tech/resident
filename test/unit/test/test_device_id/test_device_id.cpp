// The device ID is the chip's full base MAC as 12 lowercase hex digits, in
// the order the MAC is written. getEfuseMac() packs the MAC little-endian
// (byte 0 lowest), so formatting it as one number reversed the bytes and
// dropped leading zeros; the ID must read the same as the MAC on a label.
#include <unity.h>
#include "ResidentSandbox.cpp"

namespace {
Resident::Sandbox* sandbox = nullptr;
const uint64_t kDefaultMac = ESP.efuseMac;

// mac[0..5] as getEfuseMac() returns them.
uint64_t packed(const uint8_t (&mac)[6]) {
  uint64_t v = 0;
  for (int i = 0; i < 6; i++) v |= (uint64_t)mac[i] << (8 * i);
  return v;
}
}

void setUp(void) {}
void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  ESP.efuseMac = kDefaultMac;
}

void test_the_id_is_the_mac_in_order(void) {
  const uint8_t mac[6] = {0xf4, 0x12, 0xfa, 0x9c, 0x3b, 0x21};
  ESP.efuseMac = packed(mac);
  TEST_ASSERT_EQUAL_STRING("f412fa9c3b21", getDeviceId().c_str());
}

void test_leading_zeros_are_kept(void) {
  const uint8_t mac[6] = {0x00, 0x1a, 0x2b, 0x00, 0x00, 0x05};
  ESP.efuseMac = packed(mac);
  TEST_ASSERT_EQUAL_STRING("001a2b000005", getDeviceId().c_str());
}

void test_the_stub_default_is_a_real_mac(void) {
  TEST_ASSERT_EQUAL_STRING("a4cf1200cafe", getDeviceId().c_str());
}

void test_the_sandbox_and_its_ap_name_use_it(void) {
  const uint8_t mac[6] = {0xf4, 0x12, 0xfa, 0x9c, 0x3b, 0x21};
  ESP.efuseMac = packed(mac);
  Resident::SandboxConfig cfg;
  cfg.deviceType = "clock";
  Courier::Config courier;
  courier.host = "resident.inanimate.tech";
  cfg.network = courier;

  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();

  TEST_ASSERT_EQUAL_STRING("f412fa9c3b21", sandbox->getDeviceId().c_str());
  // The MAC's last bytes tell units apart; its first are the vendor's.
  TEST_ASSERT_EQUAL_STRING("Resident Clock 3b21", sandbox->getAPName().c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_id_is_the_mac_in_order);
  RUN_TEST(test_leading_zeros_are_kept);
  RUN_TEST(test_the_stub_default_is_a_real_mac);
  RUN_TEST(test_the_sandbox_and_its_ap_name_use_it);
  return UNITY_END();
}
