// src/ResidentButtonEdge.h — button edges, and the gestures made from them.
//
// The runtime takes a button as a stream of edges (ButtonEdge), each stamped
// with the time it happened, never as a level it samples. PressTracker turns
// that stream into gestures: a tap, and a hold that starts once the press has
// lasted a threshold and ends at release. Sandbox runs one for the boot
// countdown (tap = load the saved app now, long press = forget it) and one for
// the hold gesture (onSystemButtonHold).
//
// Every duration is measured between edge timestamps, or between a press's
// down edge and the time poll() is given. Never time a press from when the
// loop happens to look: across an app callback that runs for hundreds of ms,
// two quick taps that are each down when the loop samples read as one press
// held across the gap, and start a hold nobody made.
//
// Pure (no Arduino include), so it is native-tested
// (test/unit/test/test_press_tracker).
#ifndef RESIDENT_BUTTON_EDGE_H
#define RESIDENT_BUTTON_EDGE_H

#include <cstdint>

namespace Resident {

// One change of a button's debounced state. `down` is true for a press, false
// for a release; `ms` is when it happened, on the millis() clock.
struct ButtonEdge {
  bool down;
  uint32_t ms;
};

// What one call to PressTracker::edge() or poll() recognised. A hold that
// started and ended within one call sets both holdStart and holdEnd, in that
// order; a tap is never set together with either.
struct PressGestures {
  bool tap = false;
  bool holdStart = false;
  bool holdEnd = false;

  bool any() const { return tap || holdStart || holdEnd; }
};

// Replays one button's edges, in order, into gestures.
//
//   * A down edge starts a press at its timestamp. A down edge while a press
//     is already in progress is ignored.
//   * A hold starts once the press has lasted holdMs: poll(now) fires it while
//     the button is still down, with no further edge needed. A press whose up
//     edge arrives after holdMs is a hold even if poll() never saw it in time
//     (the loop was busy): it starts and ends on that one up edge.
//   * An up edge ends the press: a tap if no hold started, else the hold's end.
//   * An up edge with no press in progress (its press was dropped by reset(),
//     or its down edge went to another tracker) is ignored.
//
// Times compare as a signed 32-bit difference, so millis() wrapping is
// harmless, and a poll() time that reads earlier than a press's down edge (an
// edge stamped after the caller read the clock) counts as no time held.
class PressTracker {
public:
  explicit PressTracker(uint32_t holdMs) : _holdMs(holdMs) {}

  PressGestures edge(const ButtonEdge& e) {
    PressGestures g;
    if (e.down) {
      if (_pressing) return g;
      _pressing = true;
      _holding = false;
      _pressStartMs = e.ms;
      return g;
    }
    if (!_pressing) return g;
    if (!_holding && lasted(e.ms)) {
      g.holdStart = true;
      _holding = true;
    }
    if (_holding) g.holdEnd = true;
    else g.tap = true;
    _pressing = false;
    _holding = false;
    return g;
  }

  PressGestures poll(uint32_t nowMs) {
    PressGestures g;
    if (_pressing && !_holding && lasted(nowMs)) {
      g.holdStart = true;
      _holding = true;
    }
    return g;
  }

  // Forget the press in progress, if any, without a gesture. Its up edge, when
  // it comes, is unpaired and ignored.
  void reset() {
    _pressing = false;
    _holding = false;
  }

  bool pressing() const { return _pressing; }
  bool holding() const { return _holding; }
  uint32_t holdMs() const { return _holdMs; }

private:
  bool lasted(uint32_t ms) const {
    return static_cast<int32_t>(ms - _pressStartMs) >=
           static_cast<int32_t>(_holdMs);
  }

  uint32_t _holdMs;
  uint32_t _pressStartMs = 0;
  bool _pressing = false;
  bool _holding = false;
};

}  // namespace Resident

#endif  // RESIDENT_BUTTON_EDGE_H
