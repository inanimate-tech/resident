// src/ResidentExtension.h
#ifndef RESIDENT_EXTENSION_H
#define RESIDENT_EXTENSION_H

struct lua_State;

namespace Resident {

class LuaModule;
class Sandbox;

class Extension {
public:
  virtual const char* name() const = 0;
  virtual void registerModule(LuaModule& m) { (void)m; }   // default: no Lua module
  virtual void begin() {}                                   // hardware / module init
  virtual void update() {}                                  // per-loop tick (full rate)
  virtual void onAppReset() {}                              // app load/reload

  // The app started, stopped or was suspended (the overlay arbiter suspends
  // it while a claim sits on a surface the app draws to). Lives here rather
  // than on Driver because the extension that most needs it owns no hardware:
  // a retained-mode graphics module must stop rendering while the app is
  // suspended — a suppressed surface swallows its flushes while the library
  // marks those pixels drawn — and repaint the whole surface when it resumes.
  virtual void onAppRunning(bool running) { (void)running; }
  virtual ~Extension() = default;

  // RTTI-free downcast: returns non-null only for Driver subclasses.
  // Avoids dynamic_cast which requires -frtti (disabled by Arduino/ESP32 build).
  virtual class Driver* asDriver() { return nullptr; }

  // Drive begin() at most once. Public so user code can call it early
  // (e.g. status displays that need hardware before Sandbox initialises);
  // Sandbox calls it too, and the second call is a no-op.
  // Static rather than a member so the idempotency check lives in one
  // place and all callers (user code, Sandbox) share the same guard.
  static void beginExtension(Extension& e) {
    if (!e._begun) { e.begin(); e._begun = true; }
  }

private:
  bool _begun = false;
  friend class Sandbox;   // reads _begun and event-sink state in later tasks
};

} // namespace Resident

#endif // RESIDENT_EXTENSION_H
