#ifndef PUSH_BUTTONS_DRIVER_H
#define PUSH_BUTTONS_DRIVER_H

#include <cstdint>
#include <OutrunDriver.h>

struct PushButtonsConfig {
  uint8_t numButtons;
  const uint8_t* pins;
};

class PushButtonsDriver : public Outrun::Driver {
public:
  explicit PushButtonsDriver(const PushButtonsConfig& config);

  void begin();
  void update();

  const char* name() const override { return "button"; }
  void installSandboxModule(lua_State* L) override;
  void onAppReset() override;

  uint16_t getTotalPressCount() const;

  using LongPressFn = void(*)(bool started);
  void setLongPress(uint8_t buttonIndex, LongPressFn callback,
                    unsigned long thresholdMs = 500);

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

  static PushButtonsDriver* getFromLua(lua_State* L, const char* fn);
  static int lua_button_press_count(lua_State* L);
};

#endif // PUSH_BUTTONS_DRIVER_H
