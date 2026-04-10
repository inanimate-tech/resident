// src/OutrunDriver.h
#ifndef OUTRUN_DRIVER_H
#define OUTRUN_DRIVER_H

struct lua_State;

namespace Outrun {

struct EventField {
  const char* key;
  enum Type { INT, STRING } type;
  union {
    int i;
    const char* s;
  };
};

class Driver {
public:
  virtual const char* name() const = 0;
  virtual void installSandboxModule(lua_State* L) = 0;
  virtual void onAppReset() {}
  virtual void onAppRunning(bool running) {}
  virtual ~Driver() = default;

protected:
  void sendEvent(const char* name, const EventField* fields, int fieldCount) {
    if (_eventSinkFn) {
      _eventSinkFn(_eventSinkCtx, name, fields, fieldCount);
    }
  }

private:
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

} // namespace Outrun

#endif // OUTRUN_DRIVER_H
