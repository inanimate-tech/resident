#include <M5Unified.h>
#include <Resident.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"
#include "M5MicDriver.h"

// ---------------------------------------------------------------------------
// m5stick-voice — push-to-talk audio streaming, built on Resident core.
//
// Hold the front button: the runtime activates a "Listening" overlay (which
// suspends the app because the display is dual-role), and streams the mic as
// 16 kHz int16 PCM over the binary WebSocket. Release to stop. All of the
// suspend / display-handoff / frame-pumping lives in Resident now.
// ---------------------------------------------------------------------------

static constexpr const char* SERVER_HOST = "m5stick-voice.YOUR-CF-ACCOUNT.workers.dev";
static constexpr uint16_t SERVER_PORT = 443;

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
M5MicDriver micDriver;

// Forward declarations so ListeningOverlay::onDeactivate() can reach the
// idle prompt and the sandbox's run state; both are defined further down.
extern Resident::Sandbox sandbox;
static void showIdlePrompt();

// "Listening" overlay: drawn while the front button is held. Bound to the
// dual-role display in setup(), so activating it suspends the app.
class ListeningOverlay : public Resident::Overlay {
public:
  int priority() const override { return 100; }
  void onDraw() override { displayDriver.displayText("Listening"); }

  // The arbiter's restore() repaints the app on resume — but m5stick-voice
  // never loads an app, so that path never fires. When there's no app to
  // resume to, repaint the idle prompt ourselves so the screen doesn't stay
  // stuck on "Listening" after release.
  void onDeactivate() override {
    if (!sandbox.isAppRunning()) showIdlePrompt();
  }
};
static ListeningOverlay listening;

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "stick";
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver, &buttonDriver};
    cfg.systemDisplay = &displayDriver;   // dual-role: app screen AND system display
    cfg.systemButton  = &buttonDriver;    // front button: hold = talk
    cfg.systemMic     = &micDriver;

    Courier::Config courier;
    courier.host = SERVER_HOST;
    courier.port = SERVER_PORT;
    cfg.network  = courier;
    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

static void showIdlePrompt() {
    String msg = String("Hold button\nto talk\n") + sandbox.getDeviceId();
    displayDriver.displayText(msg.c_str());
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);

    sandbox.onTransportsWillConnect([]() {
        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(SERVER_HOST, SERVER_PORT, wsPath.c_str());
    });

    sandbox.onConnected([]() {
        static bool shown = false;
        if (shown) return;
        shown = true;
        Serial.printf("[voice] device id %s — viewer: https://%s/devices/%s/\n",
                      sandbox.getDeviceId().c_str(), SERVER_HOST,
                      sandbox.getDeviceId().c_str());
        showIdlePrompt();
    });

    sandbox.addOverlay(&listening, &displayDriver);

    // Push-to-talk: hold the front button → overlay + stream; release → stop.
    sandbox.onSystemButtonHold([](bool held) {
        sandbox.requestOverlay(&listening, held);
        if (held) sandbox.startMicStream();
        else      sandbox.stopMicStream();
    });

    sandbox.setup();
}

void loop() {
    M5.update();
    sandbox.loop();   // drives the hold detector, overlay arbiter, and mic pump
}
