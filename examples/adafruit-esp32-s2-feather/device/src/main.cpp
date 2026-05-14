#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ResidentDevice.h>

#include "DisplayDriver.h"
#include "LEDDriver.h"
#include "BatteryDriver.h"
#include "ButtonDriver.h"

static constexpr const char* RESIDENT_HOST = "resident.inanimate.tech";
static constexpr uint16_t RESIDENT_PORT = 443;

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static Adafruit_LC709203F battery;
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
static bool batteryReady = false;

static DisplayDriver displayDriver{&tft, TFT_BACKLITE};
static LEDDriver ledDriver{&pixel};
static BatteryDriver batteryDriver{&battery, &batteryReady};
static ButtonDriver buttonDriver{0};  // BOOT button on GPIO0

static Resident::DeviceConfig makeConfig() {
  Resident::DeviceConfig cfg;
  cfg.deviceType    = "feather-tft";
  cfg.host          = RESIDENT_HOST;
  // DisplayDriver dual-inherits as the StatusDisplay so connection-state
  // text gets drawn straight to the TFT before any app loads. LEDDriver
  // dual-inherits as the StatusLED so the NeoPixel reflects connection
  // state (yellow→cyan→green) until an app takes over.
  cfg.statusDisplay = &displayDriver;
  cfg.statusLED     = &ledDriver;
  cfg.extensions    = {&displayDriver, &ledDriver, &batteryDriver, &buttonDriver};
  return cfg;
}

class FeatherDevice : public Resident::Device {
 public:
  FeatherDevice() : Resident::Device(makeConfig()) {}

  // Override the default /agents/<type>-agent/<id> path with the canonical
  // /devices/<id> path used by resident.inanimate.tech.
  void onTransportsWillConnect() override {
    String wsPath = String("/devices/") + getDeviceId();
    _ws.setEndpoint(RESIDENT_HOST, RESIDENT_PORT, wsPath.c_str());
  }

  void deviceLoop() override {
    // On first successful connection, replace the StatusDisplay's text
    // with a tiny Lua app that paints the device ID on the TFT in big
    // green text and sets the NeoPixel green. The user knows what to
    // push to; a real app sent via push-app replaces this.
    static bool loaded = false;
    if (!loaded && isConnected()) {
      loaded = true;
      String app = "function init(ctx)\n"
                   "  screen.clear()\n"
                   "  screen.text(5, 5, 'Resident', 2, 0, 255, 255)\n"
                   "  screen.text(5, 30, 'feather-tft', 1)\n"
                   "  screen.text(5, 55, 'Device ID:', 1, 200, 200, 200)\n"
                   "  screen.text(5, 75, '";
      app += getDeviceId();
      app += "', 2, 0, 255, 0)\n"
             "  screen.flip()\n"
             "  led.set_brightness(20)\n"
             "  led.set(0, 255, 0)\n"
             "end\n";
      sandbox().loadApp(app.c_str());
    }
  }
};

static FeatherDevice device;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for USB-CDC enumeration on the host.

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 TFT Feather — Resident (full) ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);  // Red user LED off — NeoPixel + TFT carry status now.

  // NeoPixel: data on PIN_NEOPIXEL, power on NEOPIXEL_POWER. Variant
  // declares NEOPIXEL_POWER_ON = HIGH for this rev.
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  pixel.begin();
  pixel.setBrightness(20);
  pixel.setPixelColor(0, 0x0000FF);  // blue = booting
  pixel.show();

  // TFT_I2C_POWER gates both the TFT and the I2C bus (one pin, two rails).
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // ST7789, 240x135 portrait native. Rotation 0 keeps it portrait
  // (135 wide × 240 tall) with USB-C at the bottom.
  tft.init(135, 240);
  tft.setRotation(0);

  Wire.begin();
  if (battery.begin()) {
    battery.setPackSize(LC709203F_APA_500MAH);
    batteryReady = true;
    Serial.println("LC709203 OK");
  } else {
    Serial.println("LC709203 not found — likely no battery plugged in");
  }

  // Hand off to Resident. It owns: WiFi (via WiFiManager captive portal
  // on first boot, persisted to NVS thereafter), time sync (via ezTime),
  // WebSocket connection (via Courier), Lua sandbox lifecycle, and
  // routing inbound `app`/`shader`/`app_event` messages to the sandbox.
  // Drivers' begin() (canvas alloc, backlight PWM) is called inside
  // device.setup(), so the TFT must already be init()'d above.
  device.setup();
}

void loop() {
  device.loop();
}
