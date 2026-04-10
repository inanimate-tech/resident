// src/OutrunStatusLED.h
#ifndef OUTRUN_STATUS_LED_H
#define OUTRUN_STATUS_LED_H

#include <cstdint>

namespace Outrun {

class StatusLED {
public:
  virtual void solidColor(uint32_t color) = 0;
  virtual ~StatusLED() = default;
};

} // namespace Outrun

#endif // OUTRUN_STATUS_LED_H
