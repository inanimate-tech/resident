#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static Adafruit_LC709203F battery;
static Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
static bool batteryReady = false;

static void drawStatic() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print("ESP32-S2 TFT");

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 30);
  tft.printf("Chip:  %s @ %lu MHz", ESP.getChipModel(),
             (unsigned long)ESP.getCpuFreqMHz());
  tft.setCursor(5, 42);
  tft.printf("Flash: %lu KB",
             (unsigned long)(ESP.getFlashChipSize() / 1024));
  tft.setCursor(5, 54);
  tft.printf("PSRAM: %lu KB",
             (unsigned long)(ESP.getPsramSize() / 1024));
}

static void drawDynamic(uint32_t now, uint32_t count) {
  // Clear the bottom half (dynamic area) without flicker on the static area.
  tft.fillRect(0, 75, 240, 60, ST77XX_BLACK);

  tft.setTextSize(1);
  tft.setCursor(5, 80);
  if (batteryReady) {
    tft.setTextColor(ST77XX_GREEN);
    tft.printf("Battery: %.2fV (%.0f%%)",
               battery.cellVoltage(), battery.cellPercent());
  } else {
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("Battery: not connected");
  }

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(5, 100);
  tft.printf("Heartbeat: #%lu", (unsigned long)count);
  tft.setCursor(5, 115);
  tft.printf("Uptime: %lu s", (unsigned long)(now / 1000));
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for USB-CDC enumeration on the host.

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 TFT Feather bring-up ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() / 1024));
  Serial.printf("PSRAM: %lu KB\n",
                (unsigned long)(ESP.getPsramSize() / 1024));

  pinMode(LED_BUILTIN, OUTPUT);

  // Onboard NeoPixel: data on PIN_NEOPIXEL, power gated by NEOPIXEL_POWER.
  // The variant header declares NEOPIXEL_POWER_ON = HIGH for this rev.
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  pixel.begin();
  pixel.setBrightness(20);
  pixel.show();

  // TFT_I2C_POWER gates both the TFT and the I2C bus (STEMMA QT + onboard
  // LC709203). One pin, two rails. Drive HIGH to enable.
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // TFT backlight is a separate pin; drive HIGH to turn it on.
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);

  // ST7789, 240x135 portrait native; we want landscape (USB-C on the right).
  tft.init(135, 240);
  tft.setRotation(3);
  drawStatic();

  Wire.begin();
  Serial.println("I2C scan:");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  (no devices)");

  if (battery.begin()) {
    battery.setPackSize(LC709203F_APA_500MAH);
    batteryReady = true;
    Serial.println("LC709203 OK");
  } else {
    Serial.println("LC709203 not found — likely no battery plugged in");
  }

  // Status splash on the NeoPixel: green = LC709203 responded (battery
  // plugged in and gauge talking); yellow = no LC709203, almost always
  // because no battery is connected (the gauge is powered by VBAT).
  // Held for 2 s, then loop() takes over.
  digitalWrite(LED_BUILTIN, HIGH);
  pixel.setPixelColor(0, batteryReady ? 0x00FF00 : 0xFFFF00);
  pixel.show();
  delay(2000);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println(batteryReady ? "READY" : "READY (no battery)");
}

void loop() {
  static const uint32_t COLORS[] = {0xFF0000, 0x00FF00, 0x0000FF, 0x000000};
  static uint32_t lastBlink = 0;
  static uint32_t lastHeartbeat = 0;
  static uint8_t colorIndex = 0;
  static bool ledOn = false;
  static uint32_t count = 0;

  uint32_t now = millis();

  if (now - lastBlink >= 500) {
    lastBlink = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn);
    pixel.setPixelColor(0, COLORS[colorIndex]);
    pixel.show();
    colorIndex = (colorIndex + 1) % 4;
  }

  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    count++;
    if (batteryReady) {
      Serial.printf("[%lu ms] heartbeat #%lu — battery: %.2fV (%.0f%%)\n",
                    (unsigned long)now, (unsigned long)count,
                    battery.cellVoltage(), battery.cellPercent());
    } else {
      Serial.printf("[%lu ms] heartbeat #%lu\n",
                    (unsigned long)now, (unsigned long)count);
    }
    drawDynamic(now, count);
  }
}
