// src/ResidentSystemDisplay.h
#ifndef RESIDENT_SYSTEM_DISPLAY_H
#define RESIDENT_SYSTEM_DISPLAY_H

#include "ResidentDriver.h"

namespace Resident {

// A system-managed text display, assigned via SandboxConfig::systemDisplay.
// Lifecycle (begin/update) comes from Extension.
class SystemDisplay : public Driver {
public:
  virtual void displayText(const char* text) = 0;
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_DISPLAY_H
