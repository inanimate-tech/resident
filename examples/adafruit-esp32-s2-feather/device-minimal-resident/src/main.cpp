#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Resident.h>

// The canonical Resident relay. Devs can self-host by changing this; see the
// m5stick-demo example for the self-hosted Cloudflare Worker pattern.
static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static Adafruit_LC709203F battery;
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
static bool batteryReady = false;

// TFT-backed StatusDisplay. Resident calls displayText() with short status
// strings like "WiFi", "Connecting", "Connected" — and (once we open a WS)
// the device id, which is what the user needs to push apps. We draw it big.
class TFTStatusDisplay : public Resident::StatusDisplay {
public:
  void begin() override {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(false);
  }

  void displayText(const char* text) override {
    bool looksLikeId = (strlen(text) == 8);
    tft.fillScreen(ST77XX_BLACK);

    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(2);
    tft.setCursor(5, 5);
    tft.print("Resident");

    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 30);
    tft.print("ESP32-S2 TFT Feather");

    tft.setTextColor(looksLikeId ? ST77XX_GREEN : ST77XX_YELLOW);
    tft.setTextSize(looksLikeId ? 3 : 2);
    tft.setCursor(5, 75);
    tft.print(text);
  }
};

static TFTStatusDisplay tftStatus;

static Resident::SandboxConfig makeConfig() {
  Resident::SandboxConfig cfg;
  cfg.deviceType    = "feather-tft";
  cfg.statusDisplay = &tftStatus;
  // No Lua hardware modules yet — apps get only the sandbox-generic surface
  // (log, time, kv, math, shader globals). Next steps: expose screen.* (TFT),
  // led.* (NeoPixel), battery.* (LC709203).

  // Courier::Config has a constructor with default args, so designated
  // initializers don't compile under strict ESP-IDF builds. Use direct
  // field assignment.
  Courier::Config courier;
  courier.host = RESIDENT_HOST;
  courier.port = RESIDENT_PORT;
  cfg.network  = courier;

  return cfg;
}

Resident::Sandbox sandbox{makeConfig()};

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 TFT Feather — Resident ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());

  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  pixel.begin();
  pixel.setBrightness(20);
  pixel.setPixelColor(0, 0x0000FF);
  pixel.show();

  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  tft.init(135, 240);
  tft.setRotation(3);

  Wire.begin();
  if (battery.begin()) {
    battery.setPackSize(LC709203F_APA_500MAH);
    batteryReady = true;
    Serial.println("LC709203 OK");
  } else {
    Serial.println("LC709203 not found — likely no battery plugged in");
  }

  // Override the default /agents/<type>-agent/<id> path with the canonical
  // /devices/<id> path used by resident.inanimate.tech.
  sandbox.onTransportsWillConnect([]() {
    String wsPath = String("/devices/") + sandbox.getDeviceId();
    sandbox.ws().setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
  });

  // No bootstrap app on connect: Resident now shows the device ID itself
  // (the boot countdown screen) and auto-restores the last persisted app. A
  // hand-rolled onConnected loadApp here would cancel that restore and
  // overwrite the persisted app in NVS.

  sandbox.setup();
}

void loop() {
  sandbox.loop();

  // Bring-up indicators stay alive alongside Resident's loop:
  //   - red LED toggles at 2 Hz (the firmware is running)
  //   - NeoPixel reflects connection state (green=connected, yellow=not)
  static uint32_t lastBlink = 0;
  uint32_t now = millis();
  if (now - lastBlink >= 500) {
    lastBlink = now;
    static bool ledOn = false;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn);
    pixel.setPixelColor(0, sandbox.isConnected() ? 0x00FF00 : 0xFFFF00);
    pixel.show();
  }
}
