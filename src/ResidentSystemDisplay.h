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

  // Repaint the surface's underlying content (last app frame, idle screen,
  // prior status text) after the last overlay claiming this surface
  // releases. Called by the overlay arbiter — overlays themselves never
  // restore. Default no-op: correct for dedicated surfaces with nothing
  // underneath.
  virtual void restoreContent() {}
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_DISPLAY_H
