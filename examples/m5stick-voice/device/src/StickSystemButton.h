#pragma once
#include <Arduino.h>
#include <Resident.h>

// SystemButton reading a single active-low GPIO (the M5Stick front button).
// A level read; the runtime derives the hold gesture.
class StickSystemButton : public Resident::SystemButton {
public:
  explicit StickSystemButton(uint8_t pin) : _pin(pin) {}
  const char* name() const override { return "sysbtn"; }
  void begin() override { pinMode(_pin, INPUT_PULLUP); }
  bool pressed() override { return digitalRead(_pin) == LOW; }
private:
  uint8_t _pin;
};
