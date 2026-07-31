#include <M5Unified.h>
#include <Resident.h>
#include <ResidentM5Mic.h>   // opt-in M5 mic driver (not pulled in by Resident.h)
#include <ArduinoJson.h>
#include "le_roots.h"
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"

// The m5stick-arc server (examples/m5stick-arc/server) — an ArcAgent relay
// that answers the app's arc:ask events with inference. Deploy it with
// wrangler and put the printed hostname here (or pass it at build time:
// PLATFORMIO_BUILD_FLAGS='-DARC_HOST=\"<host>\"' pio run ...). Protocol:
//   wss://<host>/devices/<deviceId>              ← device WS (here)
//   POST https://<host>/devices/<deviceId>/send  ← push JSON to the device
//   POST https://<host>/devices/<deviceId>/arc/push ← push the arc adventure app
#ifndef ARC_HOST
#define ARC_HOST "m5stick-arc.YOUR-CF-ACCOUNT.workers.dev"
#endif
static constexpr const char* RESIDENT_HOST = ARC_HOST;
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
Resident::M5Mic micDriver;   // 16 kHz mono PCM; capture runs only while streaming

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "arc-stick";
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver, &buttonDriver};
    cfg.systemDisplay = &displayDriver;
    cfg.systemButton  = &buttonDriver;   // front button: tap = load, hold = forget
    cfg.systemMic     = &micDriver;      // push-to-talk source (see setLongPress below)

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

    // workers.dev serves a Let's Encrypt chain; this Arduino build has no IDF
    // cert bundle, and Courier's embedded fallback root (GTS Root R4) only
    // covers Cloudflare universal SSL. Pin the ISRG roots for the WS TLS.
    sandbox.onConfigureNetwork([](Courier::Client& c) {
        c.transport<Courier::WebSocketTransport>("ws").onConfigure(
            [](esp_websocket_client_config_t& cfg) { cfg.cert_pem = LE_ROOTS_PEM; });
    });

    // Override the default /agents/<type>-agent/<deviceId> path with the
    // canonical /devices/<deviceId> path used by the DeviceAgent relay.
    sandbox.onTransportsWillConnect([]() {
        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
    });

    // Push-to-talk on the front button (index 0 — also choice B on tap). The
    // driver's long-press detector both fires the hold callback AND
    // suppresses the tap event on release, so holding to talk never
    // registers as a choice. While held: a system-channel "voice" envelope
    // brackets the raw 16 kHz PCM the mic pump streams over the binary WS,
    // and the running arc app hears "ptt" events so its view can show a
    // listening state. Transcription and interpretation happen server-side.
    buttonDriver.setLongPress(0, [](bool held) {
        JsonDocument doc;
        doc["type"]  = "voice";
        doc["state"] = held ? "start" : "end";
        if (held) {
            sandbox.sendSystem(doc);         // envelope precedes binary frames
            sandbox.startMicStream();
            sandbox.sendAppEvent("ptt", "{\"state\":\"on\"}");
        } else {
            sandbox.stopMicStream();
            sandbox.sendSystem(doc);
            sandbox.sendAppEvent("ptt", "{\"state\":\"off\"}");
        }
    }, 500);

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
