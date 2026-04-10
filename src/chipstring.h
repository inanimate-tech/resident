#ifndef CHIPSTRING_H
#define CHIPSTRING_H

#include <Arduino.h>
#include <Esp.h>

// Get the full chip ID as a 64-bit value
inline uint64_t getChipId() {
  return ESP.getEfuseMac();
}

// Get the device ID as first 8 characters of lowercase hex chip ID
inline String getDeviceId() {
  uint64_t chipId = getChipId();
  String chipString = String(chipId, HEX);
  chipString.toLowerCase();

  // Return first 8 characters
  if (chipString.length() >= 8) {
    return chipString.substring(0, 8);
  }

  // Pad with zeros if shorter than 8 characters
  while (chipString.length() < 8) {
    chipString = "0" + chipString;
  }

  return chipString;
}

#endif // CHIPSTRING_H
