// NetworkReady passthrough: the onNetworkReady hook, enterNetworkReady(), and
// the status text / LED colour for Courier::State::NetworkReady. The stub
// Courier::Client records the hooks Sandbox registers so tests can fire them.
#include <unity.h>
#include <string>
#include "ResidentSandbox.cpp"

namespace {

class SpyDisplay : public Resident::SystemDisplay {
public:
  std::string last;
  const char* name() const override { return "spy-display"; }
  void displayText(const char* text) override { last = text; }
};

class SpyLED : public Resident::SystemLED {
public:
  uint32_t last = 0;
  const char* name() const override { return "spy-led"; }
  void solidColor(uint32_t color) override { last = color; }
};

SpyDisplay* display = nullptr;
SpyLED* led = nullptr;
Resident::Sandbox* sandbox = nullptr;

Resident::SandboxConfig networkedConfig() {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  cfg.systemDisplay = display;
  cfg.systemLED = led;
  Courier::Config courier;
  courier.host = "example.com";
  cfg.network = courier;
  return cfg;
}

}  // namespace

void setUp(void) {
  display = new SpyDisplay();
  led = new SpyLED();
}
void tearDown(void) {
  delete sandbox;
  sandbox = nullptr;
  delete display;
  delete led;
}

void test_courier_network_ready_fires_user_hook(void) {
  sandbox = new Resident::Sandbox(networkedConfig());
  int fired = 0;
  sandbox->onNetworkReady([&fired]() { fired++; });
  sandbox->setup();

  TEST_ASSERT_TRUE(static_cast<bool>(sandbox->courier().networkReadyCb));
  sandbox->courier().networkReadyCb();
  sandbox->courier().networkReadyCb();
  TEST_ASSERT_EQUAL_INT(2, fired);
}

void test_courier_network_ready_without_user_hook_is_harmless(void) {
  sandbox = new Resident::Sandbox(networkedConfig());
  sandbox->setup();
  sandbox->courier().networkReadyCb();
}

void test_enter_network_ready_forwards_to_courier(void) {
  sandbox = new Resident::Sandbox(networkedConfig());
  sandbox->setup();

  TEST_ASSERT_TRUE(sandbox->enterNetworkReady());
  sandbox->courier().enterNetworkReadyResult = false;
  TEST_ASSERT_FALSE(sandbox->enterNetworkReady());
  TEST_ASSERT_EQUAL_INT(2, sandbox->courier().enterNetworkReadyCalls);
}

void test_enter_network_ready_without_network_returns_false(void) {
  Resident::SandboxConfig cfg;
  cfg.deviceType = "native-test";
  sandbox = new Resident::Sandbox(cfg);
  sandbox->setup();

  TEST_ASSERT_FALSE(sandbox->enterNetworkReady());
}

void test_network_ready_state_sets_status_text_and_led(void) {
  sandbox = new Resident::Sandbox(networkedConfig());
  Courier::State seen = Courier::State::Idle;
  sandbox->onConnectionChange([&seen](Courier::State s) { seen = s; });
  sandbox->setup();

  sandbox->courier().connectionChangeCb(Courier::State::NetworkReady);

  TEST_ASSERT_EQUAL_STRING("Network ready", display->last.c_str());
  TEST_ASSERT_EQUAL_HEX32(0x00FFFF, led->last);
  TEST_ASSERT_TRUE(seen == Courier::State::NetworkReady);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_courier_network_ready_fires_user_hook);
  RUN_TEST(test_courier_network_ready_without_user_hook_is_harmless);
  RUN_TEST(test_enter_network_ready_forwards_to_courier);
  RUN_TEST(test_enter_network_ready_without_network_returns_false);
  RUN_TEST(test_network_ready_state_sets_status_text_and_led);
  return UNITY_END();
}
