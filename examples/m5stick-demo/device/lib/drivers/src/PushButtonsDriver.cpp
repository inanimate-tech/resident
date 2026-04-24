#include "PushButtonsDriver.h"
#include <Arduino.h>

#if __has_include("lua/lua.h")
#define PUSH_BUTTONS_HAS_LUA 1
extern "C" {
  #include "lua/lua.h"
  #include "lua/lualib.h"
  #include "lua/lauxlib.h"
}
#endif

PushButtonsDriver::PushButtonsDriver(const PushButtonsConfig& config)
  : _config(config)
{
  for (uint8_t i = 0; i < MAX_BUTTONS; i++) {
    _buttons[i] = {true, 0, 0, false, 0, false};
  }
}

void PushButtonsDriver::begin()
{
  uint8_t n = _config.numButtons;
  if (n > MAX_BUTTONS) n = MAX_BUTTONS;

  for (uint8_t i = 0; i < n; i++) {
    pinMode(_config.pins[i], INPUT_PULLUP);
    bool state = digitalRead(_config.pins[i]);
    _buttons[i] = {state, 0, _buttons[i].pressCount, false, 0, false};
    Serial.printf("PushButton[%d] on GPIO %d\n", i, _config.pins[i]);
  }
}

void PushButtonsDriver::update()
{
  uint8_t n = _config.numButtons;
  if (n > MAX_BUTTONS) n = MAX_BUTTONS;

  unsigned long now = millis();

  for (uint8_t i = 0; i < n; i++) {
    bool current = digitalRead(_config.pins[i]);
    auto& btn = _buttons[i];

    // Debounced press detection (HIGH → LOW)
    if (current == LOW && btn.lastState == HIGH) {
      if (now - btn.lastDebounceTime > DEBOUNCE_MS) {
        btn.lastDebounceTime = now;
        btn.isDown = true;
        btn.downStartTime = now;
        btn.longPressTriggered = false;
      }
    }

    // Check long-press threshold while held
    if (btn.isDown && !btn.longPressTriggered && _longPress[i].callback) {
      if (now - btn.downStartTime >= _longPress[i].thresholdMs) {
        btn.longPressTriggered = true;
        _longPress[i].callback(true);
      }
    }

    // Release detection (LOW → HIGH)
    if (current == HIGH && btn.lastState == LOW) {
      if (btn.isDown) {
        if (btn.longPressTriggered) {
          if (_longPress[i].callback) {
            _longPress[i].callback(false);
          }
        } else {
          btn.pressCount++;
          Outrun::EventField fields[] = {
            {"index", Outrun::EventField::INT, {.i = (int)i}},
            {"count", Outrun::EventField::INT, {.i = (int)btn.pressCount}}
          };
          sendEvent("button", fields, 2);
          Serial.printf("PushButton[%d] pressed (count=%d)\n", i, btn.pressCount);
        }
        btn.isDown = false;
      }
    }

    btn.lastState = current;
  }
}

void PushButtonsDriver::setLongPress(uint8_t buttonIndex, LongPressFn callback,
                                      unsigned long thresholdMs)
{
  if (buttonIndex < MAX_BUTTONS) {
    _longPress[buttonIndex].callback = callback;
    _longPress[buttonIndex].thresholdMs = thresholdMs;
  }
}

void PushButtonsDriver::onAppReset()
{
  for (uint8_t i = 0; i < MAX_BUTTONS; i++) {
    _buttons[i].pressCount = 0;
    // Don't clear _longPress — those are device config, not app state
  }
}

uint16_t PushButtonsDriver::getTotalPressCount() const
{
  uint16_t total = 0;
  uint8_t n = _config.numButtons;
  if (n > MAX_BUTTONS) n = MAX_BUTTONS;
  for (uint8_t i = 0; i < n; i++) {
    total += _buttons[i].pressCount;
  }
  return total;
}

#ifdef PUSH_BUTTONS_HAS_LUA

void PushButtonsDriver::installSandboxModule(lua_State* L)
{
  lua_pushlightuserdata(L, this);
  lua_setfield(L, LUA_REGISTRYINDEX, "PushButtonsDriver_instance");

  lua_newtable(L);
  lua_pushcfunction(L, lua_button_press_count);
  lua_setfield(L, -2, "press_count");
  lua_setglobal(L, "button");
}

PushButtonsDriver* PushButtonsDriver::getFromLua(lua_State* L, const char* fn)
{
  lua_getfield(L, LUA_REGISTRYINDEX, "PushButtonsDriver_instance");
  PushButtonsDriver* driver = (PushButtonsDriver*)lua_touserdata(L, -1);
  lua_pop(L, 1);

  if (!driver) {
    luaL_error(L, "%s: button driver not available", fn);
    return nullptr;
  }

  return driver;
}

int PushButtonsDriver::lua_button_press_count(lua_State* L)
{
  PushButtonsDriver* d = getFromLua(L, "button.press_count");
  if (!d) return 0;

  lua_pushinteger(L, d->getTotalPressCount());
  return 1;
}

#else

void PushButtonsDriver::installSandboxModule(lua_State*) {}

#endif
