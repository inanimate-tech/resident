#include <M5Unified.h>
#include <Resident.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"

// Set this to your custom worker's hostname — the one deployed from
// examples/server-template/ with the worker.ts from this directory dropped in.
static constexpr const char* RESIDENT_HOST = "your-worker.your-account.workers.dev";
static constexpr uint16_t RESIDENT_PORT = 443;

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

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "stick";
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver, &buttonDriver};
    cfg.statusDisplay = &displayDriver;

    Courier::Config courier;
    courier.host = RESIDENT_HOST;
    courier.port = RESIDENT_PORT;
    cfg.network  = courier;

    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

// Stash the registered timezone so the welcome app can display it.
static String g_timezone;

// POST /devices/<id>/register on the custom server and apply whatever config
// comes back. For this example, the only thing we care about is `timezone` —
// once applied via Sandbox::setTimezone, ctx.localtime_h/m in Lua reflect it.
// Runs every connect cycle, in onTransportsWillConnect (after WiFi is up,
// before transports begin) — the same lifecycle hook Hawthorn uses.
static void registerWithServer() {
    String url = String("https://") + RESIDENT_HOST +
                 "/devices/" + sandbox.getDeviceId() + "/register";
    WiFiClientSecure client;
    client.setInsecure();  // demo simplicity; pin the CA cert in production
    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.println("[register] http.begin failed");
        return;
    }
    http.addHeader("Content-Type", "application/json");
    int code = http.POST("{}");
    if (code != 200) {
        Serial.printf("[register] HTTP %d\n", code);
        http.end();
        return;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[register] JSON parse failed");
        return;
    }
    const char* tz = doc["timezone"];
    if (tz && *tz) {
        Serial.printf("[register] timezone: %s\n", tz);
        sandbox.setTimezone(tz);
        g_timezone = tz;
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);

    sandbox.onTransportsWillConnect([]() {
        // Fetch per-device config (timezone) from the custom server, then set
        // up the canonical WebSocket endpoint and let Sandbox connect.
        registerWithServer();

        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
    });

    // Welcome screen on first connect — pushed apps replace it.
    sandbox.onConnected([]() {
        static bool loaded = false;
        if (loaded) return;
        loaded = true;
        String app = "function init(ctx)\n"
                     "  screen.clear()\n"
                     "  screen.text(10, 10, 'Resident', 3)\n"
                     "  screen.text(10, 46, 'Device ID:', 2)\n"
                     "  screen.text(10, 68, '";
        app += sandbox.getDeviceId();
        app += "', 3, 0, 255, 0)\n";
        if (g_timezone.length() > 0) {
            app += "  screen.text(10, 108, 'TZ: ";
            app += g_timezone;
            app += "', 2)\n";
        }
        app += "  screen.flip()\n"
               "end\n";
        sandbox.loadApp(app.c_str());
    });

    sandbox.setup();
}

void loop() {
    M5.update();
    sandbox.loop();
}
