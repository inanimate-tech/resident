// src/OutrunModule.h
#ifndef OUTRUN_MODULE_H
#define OUTRUN_MODULE_H

struct lua_State;

namespace Outrun {

class Module {
public:
  virtual const char* name() const = 0;
  virtual void installSandboxModule(lua_State* L) = 0;
  virtual void onAppReset() {}
  virtual ~Module() = default;
};

} // namespace Outrun

#endif // OUTRUN_MODULE_H
