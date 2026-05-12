#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_LC709203F.h>

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static Adafruit_LC709203F battery;
static bool batteryReady = false;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for USB-CDC enumeration on the host.

  Serial.println();
  Serial.println("=== Adafruit ESP32-S2 Feather bring-up ===");
  Serial.printf("Chip:  %s, %d core(s) @ %lu MHz\n",
                ESP.getChipModel(),
                ESP.getChipCores(),
                (unsigned long)ESP.getCpuFreqMHz());
  Serial.printf("Flash: %lu KB\n",
                (unsigned long)(ESP.getFlashChipSize() / 1024));
  Serial.printf("PSRAM: %lu KB\n",
                (unsigned long)(ESP.getPsramSize() / 1024));

  pinMode(LED_BUILTIN, OUTPUT);

  // Onboard NeoPixel: data on PIN_NEOPIXEL, but power gated by NEOPIXEL_POWER.
  // Drive HIGH before talking to the pixel.
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  pixel.begin();
  pixel.setBrightness(20);
  pixel.show();

  // STEMMA QT + onboard LC709203 share an I2C bus gated by I2C_POWER.
  pinMode(I2C_POWER, OUTPUT);
  digitalWrite(I2C_POWER, HIGH);
  delay(10);
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
    Serial.println("LC709203 not found — continuing without battery readings");
  }
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
  }
}
