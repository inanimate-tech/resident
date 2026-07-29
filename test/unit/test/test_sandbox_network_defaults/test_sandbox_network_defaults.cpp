// Networked Sandbox construction must leave the Courier::Client with a
// default transport set. Courier 0.6+'s Client::onMessage delivers only the
// default transport's lane (see Courier::Client::dispatchJSON's "receive
// parallels send" guard); Sandbox's channel router receives exclusively
// through that callback, so an unset defaultTransport means the entire
// inbound path is silently dead on real hardware. Sandbox mirrors Courier's
// own auto-WS heuristic and defaults to "ws" when the caller left
// defaultTransport null/empty and provided a host.
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

void test_defaults_to_ws_when_unset_and_host_present(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  Courier::Config courier;
  courier.host = "resident.inanimate.tech";   // defaultTransport left null
  cfg.network = courier;

  sandbox = new Resident::Sandbox(cfg);

  TEST_ASSERT_NOT_NULL(sandbox->courier().config.defaultTransport);
  TEST_ASSERT_EQUAL_STRING("ws", sandbox->courier().config.defaultTransport);
}

void test_preserves_explicit_non_ws_default(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  Courier::Config courier;
  courier.host = "resident.inanimate.tech";
  courier.defaultTransport = "mqtt";          // caller opted into a different lane
  cfg.network = courier;

  sandbox = new Resident::Sandbox(cfg);

  TEST_ASSERT_EQUAL_STRING("mqtt", sandbox->courier().config.defaultTransport);
}

void test_no_host_leaves_default_transport_unset(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  Courier::Config courier;                    // no host, no defaultTransport
  cfg.network = courier;

  sandbox = new Resident::Sandbox(cfg);

  TEST_ASSERT_TRUE(sandbox->courier().config.defaultTransport == nullptr ||
                    sandbox->courier().config.defaultTransport[0] == '\0');
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_ws_when_unset_and_host_present);
  RUN_TEST(test_preserves_explicit_non_ws_default);
  RUN_TEST(test_no_host_leaves_default_transport_unset);
  return UNITY_END();
}
