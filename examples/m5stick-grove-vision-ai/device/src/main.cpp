#include <M5Unified.h>
#include <Resident.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"
#include "GroveVisionDriver.h"

// Default endpoint: the canonical Resident relay. Devs can self-host by
// changing RESIDENT_HOST below (or extending Courier with a config portal).
// The relay speaks the Resident canonical protocol:
//   wss://<host>/devices/<deviceId>            ← device WS (here)
//   POST https://<host>/devices/<deviceId>/send  ← skill/curl pushes JSON
static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

// M5StickS3 buttons (ESP32-S3 with OPI PSRAM): GPIO 11 + 12. GPIO 37 is part
// of the OPI PSRAM interface — reading it via digitalRead() triggers a
// watchdog reset, which is why the C Plus2 pin map doesn't apply here.
static constexpr uint8_t BUTTON_PINS[] = {11, 12};
static constexpr PushButtonsConfig buttonConfig = {.numButtons = 2, .pins = BUTTON_PINS};

DisplayDriver displayDriver;
IMUDriver imuDriver;
BuzzerDriver buzzerDriver{255};
PushButtonsDriver buttonDriver{buttonConfig};
// Grove Vision AI V2 on the Grove port (I2C). Defaults: SDA=9, SCL=10,
// 5 Hz poll, verbose serial logging of every frame (the point of this spike
// is eyeballing what each SenseCraft model emits).
GroveVisionDriver visionDriver;

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "m5stick-vision";
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver,
                         &buttonDriver, &visionDriver};
    cfg.statusDisplay = &displayDriver;

    // Courier::Config has a constructor with default args, so designated
    // initializers (.host = ...) don't compile under strict ESP-IDF builds.
    // Use direct field assignment.
    Courier::Config courier;
    courier.host = RESIDENT_HOST;
    courier.port = RESIDENT_PORT;
    cfg.network  = courier;

    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for USB CDC on M5StickS3
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);

    // Override the default /agents/<type>-agent/<deviceId> path with the
    // canonical /devices/<deviceId> path used by resident.inanimate.tech.
    sandbox.onTransportsWillConnect([]() {
        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
    });

    // On first successful connection, replace the StatusDisplay's "Connected"
    // text with a sandbox app that shows the device ID prominently (so the
    // user knows what to push to). A real app sent via push-app or
    // send-app.sh will replace this.
    sandbox.onConnected([]() {
        static bool loaded = false;
        if (loaded) return;
        loaded = true;
        String app = "function init(ctx)\n"
                     "  screen.clear()\n"
                     "  screen.text(10, 15, 'Resident', 3)\n"
                     "  screen.text(10, 60, 'Device ID:', 2)\n"
                     "  screen.text(10, 90, '";
        app += sandbox.getDeviceId();
        app += "', 3, 0, 255, 0)\n"
               "  screen.flip()\n"
               "end\n";
        sandbox.loadApp(app.c_str());
    });

    sandbox.setup();
}

void loop() {
    M5.update();
    sandbox.loop();
}
