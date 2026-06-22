// src/ResidentStatusLED.h
#ifndef RESIDENT_STATUS_LED_H
#define RESIDENT_STATUS_LED_H

#include <cstdint>
#include "ResidentDriver.h"

namespace Resident {

class StatusLED : public Driver {
public:
  virtual void solidColor(uint32_t color) = 0;
};

} // namespace Resident

#endif // RESIDENT_STATUS_LED_H
