// src/ResidentSystemLED.h
#ifndef RESIDENT_SYSTEM_LED_H
#define RESIDENT_SYSTEM_LED_H

#include <cstdint>
#include "ResidentDriver.h"

namespace Resident {

class SystemLED : public Driver {
public:
  virtual void solidColor(uint32_t color) = 0;
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_LED_H
