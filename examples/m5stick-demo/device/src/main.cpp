#include <M5Unified.h>
#include <Resident.h>
#include <ResidentLgfxModule.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"
#ifdef HAS_LVGL
#include <ResidentLvglModule.h>
#include "LVGLDriver.h"
#endif

// Default endpoint: the canonical Resident relay. Devs can self-host by
// changing RESIDENT_HOST below (or extending Courier with a config portal).
// The relay speaks the Resident canonical protocol:
//   wss://<host>/devices/<deviceId>            ← device WS (here)
//   POST https://<host>/devices/<deviceId>/send  ← skill/curl pushes JSON
static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

// Board-specific button pins. M5StickC Plus2 (ESP32 classic): GPIO 37 + 39.
// M5StickS3 (ESP32-S3 with OPI PSRAM): GPIO 11 + 12. On the S3, GPIO 37 is
// part of the OPI PSRAM interface — reading it via digitalRead() triggers a
// watchdog reset.
#if defined(BOARD_M5STICKS3)
static constexpr uint8_t BUTTON_PINS[] = {11, 12};
#else  // BOARD_M5STICK_C_PLUS2 (default)
static constexpr uint8_t BUTTON_PINS[] = {37, 39};
#endif
static constexpr PushButtonsConfig buttonConfig = {.numButtons = 2, .pins = BUTTON_PINS};

DisplayDriver displayDriver;
IMUDriver imuDriver;
BuzzerDriver buzzerDriver{255};
PushButtonsDriver buttonDriver{buttonConfig};

// lgfx: idiomatic LovyanGFX drawing from Lua (local g = lgfx.bind("main")).
// Shares the DisplayDriver's off-screen sprite; g:flip() presents it via the
// driver's repaint() path, same as screen.flip().
Resident::LgfxLovyanTarget<M5Canvas> lgfxMain{
    &displayDriver.canvas(), [] { displayDriver.repaint(); }};
Resident::LgfxModule lgfxModule;

#ifdef HAS_LVGL
// Retained-mode UI (env:m5stick-lvgl): LVGLDriver is the display/flush/tick
// glue; the Lua surface is Resident's LvglModule — apps call
// lvgl.bind("main"). The panel is shared with the sprite-backed
// screen.*/lgfx surface; last writer wins on the glass.
LVGLDriver lvglDriver;
Resident::LvglModule lvglModule;
#endif

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "stick";
    lgfxModule.addDisplay("main", &lgfxMain);
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver,
                         &buttonDriver, &lgfxModule,
#ifdef HAS_LVGL
                         &lvglDriver, &lvglModule,
#endif
    };
    cfg.systemDisplay = &displayDriver;
    cfg.systemButton  = &buttonDriver;   // front button: tap = load, hold = forget

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
    delay(2000);  // Wait for USB CDC on M5StickS3; harmless on M5Stick
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);

#ifdef HAS_LVGL
    // The lv_display_t must exist before it can be registered by name.
    // beginExtension is idempotent — the sandbox's own begin pass no-ops.
    Resident::Extension::beginExtension(lvglDriver);
    lvglModule.addDisplay("main", lvglDriver.display());
#endif

    // Override the default /agents/<type>-agent/<deviceId> path with the
    // canonical /devices/<deviceId> path used by resident.inanimate.tech.
    sandbox.onTransportsWillConnect([]() {
        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
    });

    // No bootstrap app on connect: Resident now shows the device ID itself
    // (the boot countdown screen) and auto-restores the last persisted app.
    // A hand-rolled onConnected loadApp here would cancel that restore and
    // overwrite the persisted app in NVS.

    sandbox.setup();
}

void loop() {
    M5.update();
    sandbox.loop();
}
