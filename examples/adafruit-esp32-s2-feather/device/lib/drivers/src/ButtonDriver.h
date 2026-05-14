#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <cstdint>
#include <ResidentDriver.h>
#include <ResidentLuaModule.h>

// Onboard BOOT button on GPIO0. Active-low (internal pull-up). On each
// debounced press emits a `button` driver event with index=0 and a
// running press count. Exposes `button.press_count()` to Lua.
class ButtonDriver : public Resident::Driver {
public:
  explicit ButtonDriver(uint8_t pin) : _pin(pin) {}

  const char* name() const override { return "button"; }
  void begin() override;
  void update() override;
  void onAppReset() override;
  void registerModule(Resident::LuaModule& m) override {
    m.method<ButtonDriver, &ButtonDriver::pressCount>("press_count");
  }

  int pressCount(lua_State* L);

private:
  static constexpr unsigned long DEBOUNCE_MS = 50;
  uint8_t _pin;
  bool _lastState = true;
  unsigned long _lastDebounceTime = 0;
  uint16_t _pressCount = 0;
};

#endif // BUTTON_DRIVER_H
