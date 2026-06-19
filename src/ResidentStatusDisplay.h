// src/ResidentStatusDisplay.h
#ifndef RESIDENT_STATUS_DISPLAY_H
#define RESIDENT_STATUS_DISPLAY_H

#include "ResidentDriver.h"

namespace Resident {

// A status display is a Driver that renders lines of status text. Lifecycle
// (begin/update) comes from Extension; a device assigns one via
// SandboxConfig::statusDisplay.
class StatusDisplay : public Driver {
public:
  virtual void displayText(const char* text) = 0;
};

} // namespace Resident

#endif // RESIDENT_STATUS_DISPLAY_H
