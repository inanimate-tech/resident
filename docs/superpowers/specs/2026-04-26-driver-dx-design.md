# Driver DX: standardised lifecycle + Lua-module helper

Date: 2026-04-26
Status: Draft

## Goal

Make writing and running drivers in Outrun substantially less work, both in
the user's `main.cpp` (no more manual `begin` / `update` calls per driver) and
in the driver's own implementation (no more raw `lua_State*` table-building
boilerplate).

The change is informed by a survey of every driver across Outrun's example and
across the eight projects under `~/code/hawthorn-firmware/`. The repetitive
boilerplate in `installSandboxModule` and the manual `setup`/`loop` wiring of
`begin` and `update` are both real DX leaks, present in every project.

Pre-1.0; breaking changes are acceptable in this revision.

## Non-goals

- No changes to the Lua runtime semantics (10 FPS `on_tick`, `ctx` table
  shape, app/shader/event message protocol).
- No changes to the Courier / network plumbing.
- No auto-marshalling between C++ types and Lua values. Drivers still use
  `luaL_check*` / `lua_push*` inside their per-function bodies. (See
  "Considered alternatives".)
- No changes to the existing `Outrun::Device` lifecycle ordering. `Device::setup()`
  still returns before WiFi is up; the connection runs asynchronously in `loop()`.

## Survey findings (motivation)

Patterns across the 8 hawthorn-firmware projects + the m5stick-demo example:

- Every `Driver` has a public non-virtual `begin()` that the user must call
  manually from `setup()` or `deviceSetup()`. About half also have an `update()`
  the user must call manually from `loop()` or `deviceLoop()`.
- Every `installSandboxModule(lua_State*)` follows the same shape:
  stash `this` in the Lua registry; build a flat table of `lua_CFunction`s;
  `lua_setglobal`. Most drivers also define a private `getFromLua(L, fn)`
  helper to recover `this` inside each Lua C function. ~15–30 lines of pure
  boilerplate per driver.
- A separate `Outrun::Module` class exists for "Lua-only, no events" things —
  same shape as `Driver` minus events and `onAppRunning`.
- Some status displays (`HawthornOLEDDisplay`, `StatusLCD`) are not Drivers
  at all; they implement `Outrun::StatusDisplay` and the user wires their
  `begin`/`update` manually. Some Drivers (`DisplayDriver`) dual-inherit both.

## Design

### A. Standardised lifecycle

Three phases, owned by clearly-defined parties:

| Phase | Where | What happens |
|---|---|---|
| Construction | global / top of `setup()` | Drivers instanced with config structs (pins, addresses, screen orientation, I2C bus, etc.). No work runs. |
| `begin()` | `Sandbox::initialize()` calls `ext->begin()` for each registered extension, in registration order | Hardware init, register Lua module, hook up event sink — all in one place. Idempotent via private `_begun` flag. |
| `update()` | `Sandbox::loop()` calls `ext->update()` on every iteration | Per-tick driver work (button polling, debounce timing, I2C reads). Runs at full main-loop rate, *not* gated to 10 FPS. |

Two cadences inside one `Sandbox::loop()`:

```
Sandbox::loop() {
  for each extension: ext->update();        // every call — full rate
  if (millis() - lastTick >= 100) {          // 10 FPS gate (unchanged)
    callOnTick(elapsed);                     //   → Lua on_tick(ctx, dt_ms)
  }
  processNextEvent();                        // every call
}
```

`update()` runs every iteration so debouncing and event-latency stay tight.
Lua's `on_tick` stays gated at 10 FPS via `time_ms`, exactly as today.
`update()` takes no `dt_ms`; drivers call `millis()` directly, as they all
do today.

#### Status displays — separate ownership chain

`Outrun::StatusDisplay` is a different concept from a sandbox extension —
some status displays are not drivers (HawthornOLEDDisplay, StatusLCD), and
status displays have to work without a `Sandbox` at all. So the lifecycle
hooks live on two parallel chains:

| Owner | Calls lifecycle on |
|---|---|
| `Sandbox` | Drivers / Extensions (Lua-facing) |
| `Device` (when used) | `StatusDisplay` |
| User code (no `Device`) | `StatusDisplay` manually, as today |

`StatusDisplay` gains its own `begin()` / `update()` virtuals (default
no-op). `Device::setup()` calls the status display's `begin()`;
`Device::loop()` calls its `update()`. `Sandbox` never touches
`StatusDisplay`. A driver that dual-inherits both (e.g. `DisplayDriver`) is
fine: the idempotent `_begun` flag on `Extension` prevents double-init, and
`StatusDisplay::update()` defaults to no-op.

### B. Lua-module helper (Option 2: member-function binding)

`registerModule(LuaModule& m)` replaces `installSandboxModule(lua_State* L)`.
The builder binds member functions; `this` is recovered via Lua upvalue, so
drivers no longer write registry stash + lookup boilerplate.

```cpp
class LuaModule {
public:
  // Bind a member function. `this` is recovered via upvalue at call time.
  template<auto MemberFn>
  LuaModule& method(const char* name);

  // Static fn escape hatch (no `this`).
  LuaModule& staticMethod(const char* name, lua_CFunction fn);

  // Constants
  LuaModule& constant(const char* name, int   value);
  LuaModule& constant(const char* name, double value);
  LuaModule& constant(const char* name, const char* value);
  LuaModule& constant(const char* name, bool  value);
};
```

Implementation: `method<F>` does
`lua_pushlightuserdata(L, this); lua_pushcclosure(L, &Trampoline<F>, 1);`
where `Trampoline<F>` reads the upvalue, casts to the member-fn-pointer's
class, and invokes. Zero macros, no exceptions, no allocations.

Drivers still write `lua_State*`-typed methods that use `luaL_check*` /
`lua_push*` directly — that's the deliberate cap on this design (see
"Considered alternatives").

### Class hierarchy

```cpp
namespace Outrun {

class Extension {
public:
  virtual const char* name() const = 0;
  virtual void registerModule(LuaModule& m) {}   // default: no Lua module
  virtual void begin() {}                         // hardware / module init
  virtual void update() {}                        // per-loop tick (full rate)
  virtual void onAppReset() {}                    // app load/reload
  virtual ~Extension() = default;

private:
  bool _begun = false;
  friend class Sandbox;
};

class Driver : public Extension {
public:
  virtual void onAppRunning(bool running) {}      // hardware state (LEDs off etc.)
protected:
  void sendEvent(const char* name, const EventField* fields, int count);
private:
  // event-sink machinery (unchanged from today)
};

class StatusDisplay {                             // separate chain — no Lua
public:
  virtual void begin() {}                         // NEW
  virtual void update() {}                        // NEW
  virtual void displayText(const char* text) = 0;
  virtual ~StatusDisplay() = default;
};

} // namespace Outrun
```

`Outrun::Module` is removed. Things that were Modules become `Extension`
subclasses directly. Things that emit hardware events stay as `Driver`.

### Driver before vs. after

Before (`IMUDriver`, current code):

```cpp
class IMUDriver : public Outrun::Driver {
public:
  const char* name() const override { return "imu"; }
  void installSandboxModule(lua_State* L) override;
private:
  static int lua_imu_accel(lua_State* L);
  static int lua_imu_gyro(lua_State* L);
  static int lua_imu_temp(lua_State* L);
};

void IMUDriver::installSandboxModule(lua_State* L) {
  lua_pushlightuserdata(L, this);
  lua_setfield(L, LUA_REGISTRYINDEX, "IMUDriver_instance");
  lua_newtable(L);
  lua_pushcfunction(L, lua_imu_accel); lua_setfield(L, -2, "accel");
  lua_pushcfunction(L, lua_imu_gyro);  lua_setfield(L, -2, "gyro");
  lua_pushcfunction(L, lua_imu_temp);  lua_setfield(L, -2, "temp");
  lua_setglobal(L, "imu");
}

int IMUDriver::lua_imu_accel(lua_State* L) {
  M5.Imu.update();
  auto d = M5.Imu.getImuData();
  lua_pushnumber(L, d.accel.x);
  lua_pushnumber(L, d.accel.y);
  lua_pushnumber(L, d.accel.z);
  return 3;
}
// + lua_imu_gyro, lua_imu_temp similar
```

After:

```cpp
class IMUDriver : public Outrun::Driver {
public:
  const char* name() const override { return "imu"; }
  void registerModule(Outrun::LuaModule& m) override {
    m.method<&IMUDriver::accel>("accel")
     .method<&IMUDriver::gyro>("gyro")
     .method<&IMUDriver::temp>("temp");
  }

  int accel(lua_State* L) {
    M5.Imu.update();
    auto d = M5.Imu.getImuData();
    lua_pushnumber(L, d.accel.x);
    lua_pushnumber(L, d.accel.y);
    lua_pushnumber(L, d.accel.z);
    return 3;
  }
  int gyro(lua_State* L);
  int temp(lua_State* L);
};
```

Net for `IMUDriver`: ~25 fewer lines. For `DisplayDriver` (13 functions,
all going through `getFromLua`): ~80 fewer lines.

### `main.cpp` before vs. after (m5stick-demo)

Before:

```cpp
void setup() {
  Serial.begin(115200);
  delay(2000);
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  displayDriver.begin();
  buzzerDriver.begin();
  device.setup();
}

void DemoDevice::deviceSetup() override {
  buttonDriver.begin();
  sandbox().addDriver(&displayDriver);
  sandbox().addDriver(&imuDriver);
  sandbox().addDriver(&buzzerDriver);
  sandbox().addDriver(&buttonDriver);
  sandbox().setShaderTemplate(shaderTemplate);
  sandbox().initialize();
}

void DemoDevice::deviceLoop() override {
  M5.update();
  buttonDriver.update();
  /* ... */
}
```

After:

```cpp
void setup() {
  Serial.begin(115200);
  delay(2000);
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  device.setup();
}

void DemoDevice::deviceSetup() override {
  sandbox().addDriver(&displayDriver);
  sandbox().addDriver(&imuDriver);
  sandbox().addDriver(&buzzerDriver);
  sandbox().addDriver(&buttonDriver);
  sandbox().setShaderTemplate(shaderTemplate);
  sandbox().initialize();   // calls begin() on each driver in order
}

void DemoDevice::deviceLoop() override {
  M5.update();
  /* button update happens automatically inside sandbox().loop() */
}
```

The platform-level calls (`M5.begin`, `Wire.begin`, `M5.Display.setRotation`)
still live in `setup()` because they're prerequisites for any driver
hardware init. Everything else moves to either construction or
`Sandbox::initialize()`/`loop()`.

## Breaking changes summary

- **`Outrun::Module` class is removed.** Things that were Modules become
  `Extension` subclasses directly.
- **`Driver::installSandboxModule(lua_State*)` is removed.** Replaced by
  `registerModule(LuaModule&)`.
- **`Sandbox::addModule()` is removed.** All extensions register via
  `Sandbox::addDriver()` (driver instances) or a parallel
  `Sandbox::addExtension()` for pure-Lua extensions — final shape covered
  by the registration-optics open question below.
- **Driver `begin()` / `update()` are now framework-driven.** Manual calls
  in user `setup()` / `loop()` should be deleted. Calling them manually
  before `Sandbox::initialize()` still works (idempotent via `_begun` flag),
  which is useful when driver hardware needs to come up earlier than the
  sandbox does (rare but possible).
- **`StatusDisplay` gains optional `begin()` / `update()` virtuals.**
  Default no-op, so existing implementations are not broken; `Device` will
  start calling them automatically.

## Migration

Outrun's own example (`examples/m5stick-demo`) and the eight downstream
hawthorn-firmware projects all need to migrate. The pattern per driver is:

1. Replace `installSandboxModule(lua_State*)` override with
   `registerModule(LuaModule&)`.
2. Convert each `static int lua_X(lua_State*)` to a member `int X(lua_State*)`.
3. Delete the `getFromLua(L, fn)` helper and its callsites — `this` is
   the implicit receiver.
4. Delete manual `driver.begin()` / `driver.update()` calls in main.cpp.
   Drivers that need pre-sandbox hardware (status displays mid-connect)
   may keep their early `begin()` call; the second call from
   `Sandbox::initialize()` will be a no-op.

The hawthorn-firmware projects bump their Outrun pin once Outrun's
breaking version is released, then migrate per-project.

## Considered alternatives

**Option 1 for Lua helper (tiny C-style builder):** Replace only the table-
build boilerplate; keep static `lua_CFunction`s and `getFromLua` lookups.
Saves ~3 lines per function but leaves the `this`-recovery boilerplate in
place. Rejected — too small a win.

**Option 3 for Lua helper (auto-marshalling):** Driver methods are pure
C++; the helper auto-converts args and returns. Rejected — heavy templates
on ESP32, awkward error reporting (no exceptions), `luaL_optinteger`-style
defaults fight the marshaller, and the `lua_State*` escape hatch is
something drivers genuinely need. Option 2 keeps the escape hatch open
without paying for the marshaller no driver wants.

**Merging `Driver` and `Extension` into one class:** Considered but
rejected — `Driver` carries event-sink machinery and `onAppRunning` that
pure-Lua extensions don't need. Keeping them as Extension + Driver-extends-
Extension lets a Lua-only extension exist without drag, while drivers get
the same Lua-module ergonomics for free.

**Shared base for `Driver` and `StatusDisplay`:** Rejected — different
ownership chains (Sandbox vs. Device) and `StatusDisplay` doesn't have a
Lua module, an `onAppReset`, or an `onAppRunning`. A shared base would just
be `void begin(); void update();` — not worth the type tax. Duplicating
two virtuals on two unrelated classes is fine.

## Open questions / future DX work

These came up during brainstorming and are out of scope for this revision,
but should be addressed in follow-up work:

1. **`addDriver` / `addExtension` optics.** Imperative registration after
   construction reads as procedural setup; a config-time registration
   (e.g. `DeviceConfig::extensions = {&display, &button, ...}` or a
   builder) would feel more declarative. This affects API shape — worth
   tackling alongside whatever replaces it for Sandbox standalone use.

2. **`Outrun::Device` naming.** "Device" reads as a static, inert thing in
   demos and docs, but it's actually plumbing + entrypoint. Misread at
   least once in a recent demo. Candidates to consider: `Outrun::Runtime`,
   `Outrun::Host`, `Outrun::App` — to be picked when that work happens.

## Testing

Another stream of work on this branch is adding native unit tests
(`d211b9e test: add native unit-test slot with smoke test`). The
`LuaModule` builder and the new lifecycle should land with native tests:

- Builder bind a member-fn pointer; verify Lua call recovers `this` and
  returns expected values.
- Builder bind several methods, constants of all four types; verify they
  appear in the global table.
- `Sandbox::initialize()` calls `begin()` on each registered extension
  exactly once (test the idempotent `_begun` flag with a manual
  pre-call too).
- `Sandbox::loop()` calls `update()` every iteration; verify the ratio
  of update calls to `on_tick` invocations matches the 10:1 cadence
  ratio (10 FPS Lua tick vs. ~100 Hz update calls in a 1 second window
  via the test hook).
- Status displays receive `begin()` / `update()` from `Device` but not
  from `Sandbox`.

Coordination: this work is non-conflicting with the parallel test-suite
work — the test suite adds infrastructure; this design extends what gets
tested. Both can land on the same branch.
