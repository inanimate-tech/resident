// src/ResidentStatusLED.h
#ifndef RESIDENT_STATUS_LED_H
#define RESIDENT_STATUS_LED_H

#include <cstdint>

namespace Resident {

class StatusLED {
public:
  virtual void solidColor(uint32_t color) = 0;
  virtual ~StatusLED() = default;
};

} // namespace Resident

#endif // RESIDENT_STATUS_LED_H
