// Minimal ESP-IDF example for Outrun. Demonstrates:
//   - Subclassing Outrun::Device
//   - Registering a driver with the sandbox
//   - Running setup()/loop() from app_main() rather than autostarted Arduino
//
// This intentionally targets `example.com` — it won't actually connect.
// Real consumers point `host` at their own Outrun server.

#include <Arduino.h>
#include <OutrunDevice.h>
#include "StubLEDDriver.h"

static StubLEDDriver led;

static Outrun::DeviceConfig makeConfig() {
    Outrun::DeviceConfig cfg;
    cfg.deviceType = "espidf-basic";
    cfg.host = "example.com";
    return cfg;
}

class BasicDevice : public Outrun::Device {
public:
    BasicDevice() : Outrun::Device(makeConfig()) {}

    void deviceSetup() override {
        sandbox().addDriver(&led);
        sandbox().initialize();
    }

    void deviceLoop() override {}
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
