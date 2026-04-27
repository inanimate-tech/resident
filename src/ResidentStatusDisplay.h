// src/ResidentStatusDisplay.h
#ifndef RESIDENT_STATUS_DISPLAY_H
#define RESIDENT_STATUS_DISPLAY_H

namespace Resident {

class StatusDisplay {
public:
  virtual void begin() {}                         // Device calls once at setup
  virtual void update() {}                        // Device calls every loop
  virtual void displayText(const char* text) = 0;
  virtual ~StatusDisplay() = default;
};

} // namespace Resident

#endif // RESIDENT_STATUS_DISPLAY_H
