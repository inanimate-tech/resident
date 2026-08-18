#include <M5Unified.h>
#include <Resident.h>
#include <ResidentM5Mic.h>   // opt-in M5 mic driver shipped with Resident
#include "M5Panel.h"
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"

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

// The board's one render target (R19): geometry + raw blit. The display
// driver's sprite is presented through it.
M5Panel m5Panel;

IMUDriver imuDriver;
BuzzerDriver buzzerDriver{255};
PushButtonsDriver buttonDriver{buttonConfig};
Resident::M5Mic micDriver;   // capture runs only while the pump streams

// Forward declarations so VoiceDisplay::restoreContent() can reach the
// idle prompt and the sandbox's run state; both are defined further down.
extern Resident::Sandbox sandbox;
static void showIdlePrompt();

// The display's restoreContent() repaints what's underneath once the last
// overlay claim on it releases. A resumed app repaints itself on its next
// tick; on this example (no app loaded) that means the idle prompt.
class VoiceDisplay : public DisplayDriver {
public:
  using DisplayDriver::DisplayDriver;

  void restoreContent() override {
    if (!sandbox.isAppRunning()) showIdlePrompt();
  }
};
VoiceDisplay displayDriver{&m5Panel};

// "Listening" overlay: a claim on the dual-role display while the front
// button is held — the app (if any) is suspended for the duration. Static
// text, so painting once in onAcquire is enough; no onDraw needed.
class ListeningOverlay : public Resident::Overlay {
public:
  void onAcquire() override { displayDriver.displayText("Listening"); }
};
static ListeningOverlay listening;

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "stick";
    Resident::RenderTargets::addPanel("main", &m5Panel);
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

    sandbox.addOverlay(&listening, &displayDriver, /*priority=*/100);

    // Push-to-talk: hold the front button → overlay + capture; release →
    // stop. startCapture/endCapture bracket the raw media frames with
    // {system, capture, state/stream/format} so the host knows what the
    // bytes are before the first frame arrives (the 0.8 capture dialect).
    sandbox.onSystemButtonHold([](bool held) {
        sandbox.requestOverlay(&listening, held);
        if (held) sandbox.startCapture();
        else      sandbox.endCapture();
    });

    sandbox.setup();
}

void loop() {
    M5.update();
    sandbox.loop();   // drives the hold detector, overlay arbiter, and mic pump
}
