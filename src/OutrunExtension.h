// src/OutrunExtension.h
#ifndef OUTRUN_EXTENSION_H
#define OUTRUN_EXTENSION_H

struct lua_State;

namespace Outrun {

class LuaModule;
class Sandbox;

class Extension {
public:
  virtual const char* name() const = 0;
  virtual void registerModule(LuaModule& m) { (void)m; }   // default: no Lua module
  virtual void begin() {}                                   // hardware / module init
  virtual void update() {}                                  // per-loop tick (full rate)
  virtual void onAppReset() {}                              // app load/reload
  virtual ~Extension() = default;

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

} // namespace Outrun

#endif // OUTRUN_EXTENSION_H
