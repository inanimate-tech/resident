// Minimal ESP-IDF example for Resident. Demonstrates:
//   - Constructing a Resident::Sandbox at file scope
//   - Registering a driver declaratively via SandboxConfig::extensions
//   - Running setup()/loop() from app_main() rather than autostarted Arduino
//
// This intentionally targets `example.com` — it won't actually connect.
// Real consumers point `host` at their own Resident server.

#include <Arduino.h>
#include <Resident.h>
#include "StubLEDDriver.h"

static StubLEDDriver led;

static Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType = "espidf-basic";
    cfg.extensions = {&led};

    // Courier::Config has a constructor with default args, so designated
    // initializers (`Courier::Config{.host = ...}`) don't compile under
    // strict ESP-IDF builds. Use direct field assignment.
    Courier::Config courier;
    courier.host = "example.com";
    cfg.network  = courier;

    return cfg;
}

static Resident::Sandbox sandbox{makeConfig()};

extern "C" void app_main() {
    initArduino();
    sandbox.setup();
    while (true) {
        sandbox.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
