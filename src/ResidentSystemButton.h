// src/ResidentSystemButton.h
#ifndef RESIDENT_SYSTEM_BUTTON_H
#define RESIDENT_SYSTEM_BUTTON_H

#include "ResidentDriver.h"

namespace Resident {

// A single hardware button read directly by the runtime — distinct from the
// app-facing button driver (whose events are gated on app-running state).
//
// pressed() is a *level* read: return true for as long as the button is
// physically held (debounced by the implementation), false when released. The
// runtime derives gestures from it — during the boot countdown a tap (quick
// press + release) loads the saved app immediately, and a long press forgets
// it. A natural home for future core button uses (e.g. hold-to-enter WiFi
// config).
class SystemButton : public Driver {
public:
  virtual bool pressed() = 0;
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_BUTTON_H
