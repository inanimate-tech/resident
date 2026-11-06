// src/ResidentGuardedDispatch.h — the sandbox's execution guard, for an
// extension that runs app Lua from its own C code.
//
// The sandbox guards every Lua call it makes itself (init, a tick, an event,
// a chunk): a wall-clock deadline (executionDeadlineMs) aborts a runaway, and
// the call counts as a dispatch for everything else that keys on one. An
// extension can call app Lua too, outside any of those: a graphics library's
// timer, animation and input callbacks, or a widget's per-frame function, all
// run from the extension's update(). Those calls are app code, and a loop in
// one would otherwise hold the device for as long as it spins.
//
// The sandbox hands every extension a DispatchGuard before begin()
// (Extension::setDispatchGuard). An extension holds a GuardedDispatch over a
// stretch of C code that may call into Lua:
//
//   void update() override {
//     Resident::GuardedDispatch dispatch(_guard, "mylib");
//     mylib_run_callbacks();
//   }
//
// The whole stretch is ONE dispatch with one deadline, not one per callback:
// N runaway callbacks cost one deadline, not N. Once it has passed, every
// Lua instruction raises until the scope ends, so a callback that is still
// running stops at its next instruction and one that starts later stops at
// its first.
//
// The raise is a Lua error, so the C code inside the scope must reach Lua
// only through a protected call (lua_pcall): the error then ends in that
// call, the C code sees an ordinary failed call, and nothing ever longjmps
// out of the library. Code that calls Lua unprotected is not safe to guard.
//
// When the deadline ended the scope's Lua, the sandbox reports it as a
// `runtime_error` naming the source, and counts it against the app as it
// does an aborted tick. Scopes nest: an inner one joins the outer one's
// dispatch and deadline.
#ifndef RESIDENT_GUARDED_DISPATCH_H
#define RESIDENT_GUARDED_DISPATCH_H

namespace Resident {

// Implemented by the sandbox. Extensions use it through GuardedDispatch.
class DispatchGuard {
public:
  // Starts a dispatch: arms the deadline unless one is already running.
  virtual void enterDispatch() = 0;
  // Ends it. `source` names the caller in the report when the deadline
  // stopped its Lua.
  virtual void exitDispatch(const char* source) = 0;

protected:
  ~DispatchGuard() = default;
};

// One guarded dispatch, for the lifetime of the object. A null guard (an
// extension used outside a sandbox) guards nothing.
class GuardedDispatch {
public:
  GuardedDispatch(DispatchGuard* guard, const char* source)
      : _guard(guard), _source(source) {
    if (_guard) _guard->enterDispatch();
  }
  ~GuardedDispatch() {
    if (_guard) _guard->exitDispatch(_source);
  }
  GuardedDispatch(const GuardedDispatch&) = delete;
  GuardedDispatch& operator=(const GuardedDispatch&) = delete;

private:
  DispatchGuard* _guard;
  const char* _source;
};

} // namespace Resident

#endif // RESIDENT_GUARDED_DISPATCH_H
