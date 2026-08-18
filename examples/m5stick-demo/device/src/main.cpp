#include <M5Unified.h>
#include <Resident.h>
#include <ResidentLgfxModule.h>
#include "M5Panel.h"
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"
#ifdef HAS_LVGL
#include <ResidentLvglModule.h>
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

// The board's ONE render target (R19): geometry + raw blit. Everything that
// reaches the glass goes through it — the status/app sprite, lgfx's frame
// push, LVGL's partial flushes.
M5Panel m5Panel;

DisplayDriver displayDriver{&m5Panel};
IMUDriver imuDriver;
BuzzerDriver buzzerDriver{255};
PushButtonsDriver buttonDriver{buttonConfig};

// lgfx: idiomatic LovyanGFX drawing from Lua (local g = lgfx.bind("main")).
// Shares the DisplayDriver's off-screen sprite; g:flip() blits it through
// m5Panel — the same pixels screen.flip() pushes, the same panel LVGL
// flushes to. Binding claims the panel (R19).
Resident::LgfxSpriteTarget<M5Canvas> lgfxMain{&displayDriver.canvas()};
Resident::LgfxModule lgfxModule;

#ifdef HAS_LVGL
// Retained-mode UI (env:m5stick-lvgl). No glue driver: the module owns
// lv_init, the lv_display_t over m5Panel, its draw buffers, the flush and
// the timer pump. Apps call lvgl.bind("main") — which CLAIMS the panel, so
// the lgfx path stands down instead of fighting for the glass (R19).
Resident::LvglModule lvglModule;
#endif

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "stick";
    Resident::RenderTargets::addPanel("main", &m5Panel);
    lgfxModule.addDisplay("main", &lgfxMain);
#ifdef HAS_LVGL
    Resident::LvglModule::DisplayOptions lvglMain;
    lvglMain.dpi = 240;          // 240x135 on 25x13mm of glass
    lvglMain.bufferRows = 14;    // ~1/10 screen: 240 * 14 * 2B = 6.7KB
    lvglModule.addDisplay("main", lvglMain);
#endif
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver,
                         &buttonDriver, &lgfxModule,
#ifdef HAS_LVGL
                         &lvglModule,
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
