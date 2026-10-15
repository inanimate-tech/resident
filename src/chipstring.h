#ifndef CHIPSTRING_H
#define CHIPSTRING_H

#include <Arduino.h>
#include <Esp.h>

// Get the full chip ID as a 64-bit value: the factory base MAC from eFuse,
// its six bytes packed little-endian (MAC byte 0 is the lowest byte).
inline uint64_t getChipId() {
  return ESP.getEfuseMac();
}

// Get the device ID: the full base MAC as 12 lowercase hex digits, in the
// order the MAC is written (aa:bb:cc:dd:ee:ff -> "aabbccddeeff"), leading
// zeros kept. The same string esptool and a device label print, minus the colons.
inline String getDeviceId() {
  uint64_t chipId = getChipId();
  char id[13];
  for (int i = 0; i < 6; i++) {
    snprintf(id + 2 * i, 3, "%02x", (unsigned)((chipId >> (8 * i)) & 0xff));
  }
  return String(id);
}

#endif // CHIPSTRING_H
