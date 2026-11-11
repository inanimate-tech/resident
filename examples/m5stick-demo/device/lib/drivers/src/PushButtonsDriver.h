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
// runtime takes its edges directly for the boot countdown (tap = load now, long
// press = forget the saved app) and the hold gesture, independent of the
// app-facing button events.
//
// The pins give only a level, so update() samples them and queues an edge for
// button 0 at each debounced change, stamped at the sample that saw it. update()
// and the runtime's drain both run on the loop task, so the queue needs no
// locking. Sampling once per loop pass makes the edges only as timely as the
// loop; a board whose button raises an interrupt would queue them from there.
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

  // Resident::SystemButton — button 0's queued edges, oldest first, filled by
  // update() (called every loop while this is the system button).
  bool takeEdge(Resident::ButtonEdge& edge) override;

  uint16_t getTotalPressCount() const;

  using LongPressFn = void(*)(bool started);
  void setLongPress(uint8_t buttonIndex, LongPressFn callback,
                    unsigned long thresholdMs = 200);

private:
  static constexpr uint8_t MAX_BUTTONS = 4;
  static constexpr unsigned long DEBOUNCE_MS = 50;
  // Hold recognition threshold when no firmware long-press callback set a
  // custom one — `hold` events are emitted either way (recognition belongs
  // to the driver).
  static constexpr unsigned long DEFAULT_HOLD_MS = 500;

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

  // Button 0's edges, waiting for takeEdge(). Drained every loop pass, so it
  // holds at most a press and a release; if it is ever full, a new edge is
  // dropped (the runtime ignores the unpaired edge that leaves).
  static constexpr uint8_t EDGE_QUEUE_LEN = 8;
  Resident::ButtonEdge _edges[EDGE_QUEUE_LEN] = {};
  uint8_t _edgeHead = 0;
  uint8_t _edgeCount = 0;
  void queueEdge(bool down, unsigned long ms);
};

#endif // PUSH_BUTTONS_DRIVER_H
