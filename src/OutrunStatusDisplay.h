// src/OutrunStatusDisplay.h
#ifndef OUTRUN_STATUS_DISPLAY_H
#define OUTRUN_STATUS_DISPLAY_H

namespace Outrun {

class StatusDisplay {
public:
  virtual void begin() {}                         // Device calls once at setup
  virtual void update() {}                        // Device calls every loop
  virtual void displayText(const char* text) = 0;
  virtual ~StatusDisplay() = default;
};

} // namespace Outrun

#endif // OUTRUN_STATUS_DISPLAY_H
