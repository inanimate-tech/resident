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
// Only Courier::Config is needed at test-compile time: it's a POD passed by
// value through Resident's SandboxConfig::network. Mirror new Courier::Config
// fields here when they're added upstream. Last sync: courier 0.4.x — see
// /Users/matt/code/courier/src/Courier.h struct Config.
#pragma once
#include <cstdint>

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

} // namespace Courier
