// Minimal ESP-IDF example for Resident. Demonstrates:
//   - Subclassing Resident::Device
//   - Registering a driver declaratively via DeviceConfig::extensions
//   - Running setup()/loop() from app_main() rather than autostarted Arduino
//
// This intentionally targets `example.com` — it won't actually connect.
// Real consumers point `host` at their own Resident server.

#include <Arduino.h>
#include <ResidentDevice.h>
#include "StubLEDDriver.h"

static StubLEDDriver led;

static Resident::DeviceConfig makeConfig() {
    Resident::DeviceConfig cfg;
    cfg.deviceType = "espidf-basic";
    cfg.host = "example.com";
    cfg.extensions = {&led};
    return cfg;
}

class BasicDevice : public Resident::Device {
public:
    BasicDevice() : Resident::Device(makeConfig()) {}
};

static BasicDevice device;

extern "C" void app_main() {
    initArduino();
    device.setup();
    while (true) {
        device.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
