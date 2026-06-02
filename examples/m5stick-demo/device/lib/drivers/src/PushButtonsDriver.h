#ifndef PUSH_BUTTONS_DRIVER_H
#define PUSH_BUTTONS_DRIVER_H

#include <cstdint>
#include <ResidentDriver.h>
#include <ResidentLuaModule.h>

struct PushButtonsConfig {
  uint8_t numButtons;
  const uint8_t* pins;
};

class PushButtonsDriver : public Resident::Driver {
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
};

#endif // PUSH_BUTTONS_DRIVER_H
