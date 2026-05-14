#include "ButtonDriver.h"
#include <Arduino.h>

extern "C" {
  #include "lua/lua.h"
  #include "lua/lauxlib.h"
}

void ButtonDriver::begin() {
  pinMode(_pin, INPUT_PULLUP);
  _lastState = digitalRead(_pin);
  Serial.printf("Button on GPIO %u\n", (unsigned)_pin);
}

void ButtonDriver::update() {
  bool current = digitalRead(_pin);
  unsigned long now = millis();
  if (current == LOW && _lastState == HIGH) {
    if (now - _lastDebounceTime > DEBOUNCE_MS) {
      _lastDebounceTime = now;
      _pressCount++;
      Resident::EventField fields[] = {
        {"index", Resident::EventField::INT, {.i = 0}},
        {"count", Resident::EventField::INT, {.i = (int)_pressCount}}
      };
      sendEvent("button", fields, 2);
      Serial.printf("Button pressed (count=%u)\n", (unsigned)_pressCount);
    }
  }
  _lastState = current;
}

void ButtonDriver::onAppReset() {
  _pressCount = 0;
}

int ButtonDriver::pressCount(lua_State* L) {
  lua_pushinteger(L, _pressCount);
  return 1;
}
