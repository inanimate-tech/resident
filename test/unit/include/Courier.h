// test/unit/include/Courier.h
//
// Minimal Courier::Config stub for native unit tests.
//
// The real <Courier.h> (in inanimate/courier) pulls in Arduino.h, WiFiManager.h,
// esp_websocket_client.h, mqtt_client.h, ezTime — none of which compile under
// the native PlatformIO env. Production code (examples/*/) includes the real
// header via PIO's lib_deps; native tests get this stub via -Iinclude
// (already in build_flags).
//
// Courier::Config is a POD passed by value through Resident's
// SandboxConfig::network. State / Client / WebSocketTransport mirror just
// the type-shape ResidentSandbox.cpp compiles against; the Client methods
// are no-ops because native tests run networkless (cfg.network unset means
// Sandbox never constructs or calls into the Client at runtime). Mirror new
// fields/methods here when they're added upstream. Last sync: courier 0.4.x
// — see the upstream Courier.h (github.com/inanimate-tech/courier).
#pragma once
#include <cstdint>
#include <functional>
#include <ArduinoJson.h>

namespace Courier {

struct Config {
  const char* host;
  uint16_t port;
  const char* path;
  const char* apName;
  const char* defaultTransport;
  uint32_t dns1;
  uint32_t dns2;

  Config(const char* host = nullptr,
         uint16_t port = 443,
         const char* path = "/",
         const char* apName = nullptr,
         const char* defaultTransport = nullptr,
         uint32_t dns1 = 0,
         uint32_t dns2 = 0)
      : host(host), port(port), path(path), apName(apName),
        defaultTransport(defaultTransport), dns1(dns1), dns2(dns2) {}
};

// Connection states ResidentSandbox.cpp switches on. Values beyond these
// exist upstream; the sandbox handles unknowns via `default:`.
enum class State {
  Idle,
  WifiConnecting,
  WifiConfiguring,
  WifiConnected,
  TransportsConnecting,
  Connected,
  Reconnecting,
  ConnectionFailed,
};

class WebSocketTransport {
public:
  void setEndpoint(const char* host, uint16_t port, const char* path) {
    (void)host; (void)port; (void)path;
  }
};

class Client {
public:
  explicit Client(const Config& config) { (void)config; }

  template <typename T>
  T& transport(const char* name) {
    (void)name;
    static T instance;
    return instance;
  }

  State getState() const { return State::Idle; }
  bool isTimeSynced() const { return false; }
  void setup() {}
  void loop() {}
  void setAPName(const char* name) { (void)name; }

  void onMessage(std::function<void(const char*, const char*, JsonDocument&)>) {}
  void onConnectionChange(std::function<void(State)>) {}
  void onTransportsWillConnect(std::function<void()>) {}
  void onConnected(std::function<void()>) {}
};

} // namespace Courier
