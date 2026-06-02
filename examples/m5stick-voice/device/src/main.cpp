#include <M5Unified.h>
#include <Resident.h>
#include "DisplayDriver.h"
#include "IMUDriver.h"
#include "BuzzerDriver.h"
#include "PushButtonsDriver.h"

// ---------------------------------------------------------------------------
// m5stick-voice — Milestone 1: push-to-talk audio streaming.
//
// Hold the front button to stream the mic as 16 kHz int16 PCM over a binary
// WebSocket. Audio rides the device's normal Resident relay connection
// (/devices/<id>); the m5stick-voice server transcribes it via the OpenAI
// Realtime API and serves a live transcript + FFT viewer.
// ---------------------------------------------------------------------------

// Your deployed m5stick-voice worker. Replace YOUR-CF-ACCOUNT with your
// Cloudflare workers.dev subdomain (or a custom domain). The device connects
// to the Resident relay path /devices/<deviceId> and streams audio as binary
// frames on that same socket.
static constexpr const char* SERVER_HOST = "m5stick-voice.YOUR-CF-ACCOUNT.workers.dev";
static constexpr uint16_t SERVER_PORT = 443;

// Board-specific button pins. M5StickC Plus2 (ESP32 classic): GPIO 37 + 39.
// M5StickS3 (ESP32-S3 with OPI PSRAM): GPIO 11 + 12. On the S3, GPIO 37 is
// part of the OPI PSRAM interface — reading it via digitalRead() triggers a
// watchdog reset. (Same mapping as m5stick-demo.)
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

// ---- Audio capture config (lifted from courier binary-websocket example) ----
static constexpr uint32_t SAMPLE_RATE   = 16000;  // M5.Mic default
static constexpr size_t   FRAME_SAMPLES = 512;    // PCM samples per WS frame
static constexpr size_t   RING_SLOTS    = 4;      // ring of capture buffers

static int16_t audioRing[RING_SLOTS][FRAME_SAMPLES];
static size_t  recIdx  = 2;   // slot M5.Mic.record() is filling now
static size_t  sendIdx = 0;   // completed slot ready to send (lags recIdx by 2)

static volatile bool streaming = false;

// ---- Serial telemetry for the audio path -----------------------------------
// Kept in deliberately: a once-per-second [voice] stat line is the quickest way
// to confirm a clean stream when bringing this up on new hardware or a new
// backend. The fields distinguish the failure modes (see the heartbeat below).
static uint32_t dbgHoldStarts = 0;   // times onHold(true) fired
static uint32_t dbgFramesRec  = 0;   // M5.Mic.record() returned true
static uint32_t dbgFramesSent = 0;   // sendBinary() returned true
static uint32_t dbgSendFail   = 0;   // sendBinary() returned false
static unsigned long dbgLastStat = 0;
static unsigned long dbgLastSendMs = 0;  // millis of previous queued frame
static uint32_t dbgMaxGapMs = 0;     // worst inter-frame gap this second
static unsigned long dbgMaxLoopMs = 0;   // worst single loop() duration this second
static unsigned long dbgLoopMark = 0;

static const char* stateName(Courier::State s) {
    switch (s) {
        case Courier::State::Booting:              return "Booting";
        case Courier::State::WifiConnecting:       return "WifiConnecting";
        case Courier::State::WifiConnected:        return "WifiConnected";
        case Courier::State::WifiConfiguring:      return "WifiConfiguring";
        case Courier::State::TransportsConnecting: return "TransportsConnecting";
        case Courier::State::Connected:            return "Connected";
        case Courier::State::Reconnecting:         return "Reconnecting";
        case Courier::State::ConnectionFailed:     return "ConnectionFailed";
        default:                                   return "?";
    }
}

Resident::SandboxConfig makeConfig() {
    Resident::SandboxConfig cfg;
    cfg.deviceType    = "stick";
    cfg.extensions    = {&displayDriver, &imuDriver, &buzzerDriver, &buttonDriver};
    cfg.statusDisplay = &displayDriver;

    // Courier::Config has a constructor with default args, so designated
    // initializers (.host = ...) don't compile under strict ESP-IDF builds.
    // Use direct field assignment. The endpoint is repointed in
    // onTransportsWillConnect, but seed host/port here too.
    Courier::Config courier;
    courier.host = SERVER_HOST;
    courier.port = SERVER_PORT;
    cfg.network  = courier;

    return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

// Push-to-talk. A plain C function pointer — cannot capture, so it works
// against the file-scope globals above. The same callback fires for both
// edges: started=true once the hold passes the threshold, started=false on
// release.
static void onHold(bool started) {
    if (started) {
        recIdx  = 2;
        sendIdx = 0;
        dbgFramesRec = dbgFramesSent = dbgSendFail = 0;
        dbgHoldStarts++;
        streaming = true;
        displayDriver.displayText("Listening");
        Serial.printf("[voice] %lu HOLD start (#%lu) -> streaming\n",
                      millis(), dbgHoldStarts);
    } else {
        streaming = false;
        displayDriver.displayText("Hold button\nto talk");
        Serial.printf("[voice] %lu HOLD end -> stopped "
                      "(rec=%lu sent=%lu fail=%lu)\n",
                      millis(), dbgFramesRec, dbgFramesSent, dbgSendFail);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for USB CDC on M5StickS3; harmless on M5Stick
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);

    // S3: mic and speaker share the ES8311 codec / I2S port. Begin the mic once
    // here — M5.Mic.begin() releases the speaker's I2S driver internally. We
    // keep it running and gate sending with `streaming`. (Toggling begin/end on
    // every press logged repeated "I2S port 0 has not installed" errors.)
    M5.Mic.begin();

    // Connect on the Resident relay path; audio rides this same socket.
    sandbox.onTransportsWillConnect([]() {
        String wsPath = String("/devices/") + sandbox.getDeviceId();
        sandbox.ws().setEndpoint(SERVER_HOST, SERVER_PORT, wsPath.c_str());
    });

    // Log every connection-state transition with a timestamp. A socket drop
    // mid-stream surfaces here (and as connected=0 in the stat line).
    sandbox.onConnectionChange([](Courier::State s) {
        Serial.printf("[voice] %lu state -> %s\n", millis(), stateName(s));
    });

    // Show the idle prompt once connected (replaces the sandbox's own
    // "Connecting…"/"Connected" status text). Function-local static guards
    // against re-firing on reconnect.
    sandbox.onConnected([]() {
        static bool shown = false;
        if (shown) return;
        shown = true;
        Serial.printf("[voice] device id %s — viewer: https://%s/devices/%s/\n",
                      sandbox.getDeviceId().c_str(), SERVER_HOST,
                      sandbox.getDeviceId().c_str());
        if (!streaming) displayDriver.displayText("Hold button\nto talk");
    });

    // Push-to-talk on button 0 (the front button). Uses the 200ms default
    // threshold so a tap is rejected but talk starts promptly.
    buttonDriver.setLongPress(0, onHold);

    sandbox.setup();
}

void loop() {
    // Track the worst loop() duration per second. A periodic stall would let
    // the mic DMA back up and then drain in a burst (even per-second totals,
    // but uneven on the wire), so maxLoop is the tell-tale for stutter.
    unsigned long loopStart = millis();
    if (dbgLoopMark) {
        unsigned long d = loopStart - dbgLoopMark;
        if (d > dbgMaxLoopMs) dbgMaxLoopMs = d;
    }
    dbgLoopMark = loopStart;

    M5.update();
    sandbox.loop();  // drives buttonDriver.update(), which fires onHold

    // While holding, capture a frame and send the completed (lagging) slot.
    if (streaming && M5.Mic.isEnabled()) {
        int16_t* rec = audioRing[recIdx];
        if (M5.Mic.record(rec, FRAME_SAMPLES, SAMPLE_RATE)) {
            dbgFramesRec++;
            unsigned long nowMs = millis();
            if (dbgLastSendMs) {
                unsigned long gap = nowMs - dbgLastSendMs;
                if (gap > dbgMaxGapMs) dbgMaxGapMs = gap;
            }
            dbgLastSendMs = nowMs;
            int16_t* ready = audioRing[sendIdx];
            bool ok = sandbox.ws().sendBinary(reinterpret_cast<const uint8_t*>(ready),
                                              FRAME_SAMPLES * sizeof(int16_t));
            if (ok) dbgFramesSent++; else dbgSendFail++;
            sendIdx = (sendIdx + 1) % RING_SLOTS;
            recIdx  = (recIdx  + 1) % RING_SLOTS;
        }
    }

    // Once-per-second heartbeat while streaming. Reading the fields:
    //   line stops printing      -> loop blocked (e.g. sendBinary portMAX_DELAY)
    //   micEn=0                  -> mic/I2S lost
    //   fail climbing, connected=1 -> WS send backpressure
    //   connected=0              -> socket dropped
    //   maxGap/maxLoop spiking   -> periodic stall -> bursty (stuttery) stream
    if (streaming) {
        unsigned long now = millis();
        if (now - dbgLastStat >= 1000) {
            dbgLastStat = now;
            Serial.printf("[voice] %lu stat rec=%lu sent=%lu fail=%lu micEn=%d connected=%d "
                          "maxGap=%lums maxLoop=%lums\n",
                          now, dbgFramesRec, dbgFramesSent, dbgSendFail,
                          (int)M5.Mic.isEnabled(), (int)sandbox.isConnected(),
                          dbgMaxGapMs, dbgMaxLoopMs);
            dbgMaxGapMs = 0;
            dbgMaxLoopMs = 0;
        }
    }
}
