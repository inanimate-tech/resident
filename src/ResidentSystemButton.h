// src/ResidentSystemButton.h
#ifndef RESIDENT_SYSTEM_BUTTON_H
#define RESIDENT_SYSTEM_BUTTON_H

namespace Resident {

// A single hardware button read directly by the runtime — distinct from the
// app-facing button driver (whose events are gated on app-running state).
// Currently used to skip the boot countdown; a natural home for future core
// button uses (e.g. hold-to-enter WiFi config).
class SystemButton {
public:
  virtual bool pressed() = 0;
  virtual ~SystemButton() = default;
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_BUTTON_H
