#ifndef PUSH_BUTTONS_DRIVER_H
#define PUSH_BUTTONS_DRIVER_H

#include <cstdint>
#include <ResidentDriver.h>
#include <ResidentLuaModule.h>
#include <ResidentSystemButton.h>

struct PushButtonsConfig {
  uint8_t numButtons;
  const uint8_t* pins;
};

// Also acts as the Resident::SystemButton (button 0 — the front button), so the
// runtime can read it directly during the boot countdown (tap = load now, long
// press = forget the saved app), independent of the app-facing button events.
class PushButtonsDriver : public Resident::SystemButton {
public:
  explicit PushButtonsDriver(const PushButtonsConfig& config);

  const char* name() const override { return "button"; }
  void begin() override;
  void update() override;
  void onAppReset() override;
  void registerModule(Resident::LuaModule& m) override {
    m.method<PushButtonsDriver, &PushButtonsDriver::pressCount>("press_count");
  }

  int pressCount(lua_State* L);

  // Resident::SystemButton — level read of button 0. Self-debounced so it
  // works during the boot countdown, when the Driver's update() isn't called.
  bool pressed() override;

  uint16_t getTotalPressCount() const;

  using LongPressFn = void(*)(bool started);
  void setLongPress(uint8_t buttonIndex, LongPressFn callback,
                    unsigned long thresholdMs = 200);

private:
  static constexpr uint8_t MAX_BUTTONS = 4;
  static constexpr unsigned long DEBOUNCE_MS = 50;

  const PushButtonsConfig& _config;

  struct ButtonState {
    bool lastState;
    unsigned long lastDebounceTime;
    uint8_t pressCount;
    bool isDown;
    unsigned long downStartTime;
    bool longPressTriggered;
  };

  ButtonState _buttons[MAX_BUTTONS];

  struct LongPressConfig {
    LongPressFn callback = nullptr;
    unsigned long thresholdMs = 500;
  };
  LongPressConfig _longPress[MAX_BUTTONS];

  // Independent debounce for the SystemButton level read (pressed()), since it
  // is polled outside the update() cycle during the boot countdown.
  bool _sysRawLast = false;
  unsigned long _sysRawChangedAt = 0;
  bool _sysStable = false;
};

#endif // PUSH_BUTTONS_DRIVER_H
