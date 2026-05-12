#include <Arduino.h>

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
}

void loop() {
  static uint32_t lastHeartbeat = 0;
  static uint32_t count = 0;
  uint32_t now = millis();
  if (now - lastHeartbeat >= 1000) {
    lastHeartbeat = now;
    count++;
    Serial.printf("[%lu ms] heartbeat #%lu\n",
                  (unsigned long)now, (unsigned long)count);
  }
}
