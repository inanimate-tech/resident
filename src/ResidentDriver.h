// src/ResidentDriver.h
#ifndef RESIDENT_DRIVER_H
#define RESIDENT_DRIVER_H

#include "ResidentExtension.h"

namespace Resident {

struct EventField {
  const char* key;
  enum Type { INT, STRING } type;
  union {
    int i;
    const char* s;
  };
};

class Driver : public Extension {
public:
  // Most lifecycle (name, registerModule, begin, update, onAppReset) is
  // inherited from Extension. Driver adds the hardware-state hook and
  // the event-sink machinery.
  virtual void onAppRunning(bool running) { (void)running; }

  // RTTI-free downcast support (Extension::asDriver returns nullptr by default).
  Driver* asDriver() override { return this; }

protected:
  void sendEvent(const char* name, const EventField* fields, int fieldCount) {
    if (_eventSinkFn) {
      _eventSinkFn(_eventSinkCtx, name, fields, fieldCount);
    }
  }

private:
  // Plain fn pointer (not std::function) — keeps Driver allocation-free
  // for embedded targets; Sandbox provides the ctx for closure-like data.
  using EventSinkFn = void(*)(void* ctx, const char* name,
                              const EventField* fields, int fieldCount);
  EventSinkFn _eventSinkFn = nullptr;
  void* _eventSinkCtx = nullptr;

  friend class Sandbox;
  void setEventSink(EventSinkFn fn, void* ctx) {
    _eventSinkFn = fn;
    _eventSinkCtx = ctx;
  }
};

} // namespace Resident

#endif // RESIDENT_DRIVER_H
