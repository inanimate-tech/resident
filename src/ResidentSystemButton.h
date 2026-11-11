// src/ResidentSystemButton.h
#ifndef RESIDENT_SYSTEM_BUTTON_H
#define RESIDENT_SYSTEM_BUTTON_H

#include "ResidentButtonEdge.h"
#include "ResidentDriver.h"

namespace Resident {

// A single hardware button the runtime takes directly — distinct from the
// app-facing button driver (whose events are gated on app-running state).
// The runtime makes its gestures from it: during the boot countdown a tap
// loads the saved app now and a long press (1 s) forgets it, and outside the
// countdown a hold (500 ms) drives Sandbox::onSystemButtonHold.
//
// The runtime takes the button as edges, never as a level. Sandbox calls
// takeEdge() on every loop pass until it returns false, and times every
// gesture from the edges' own timestamps (PressTracker, ResidentButtonEdge.h),
// so a press made while an app callback held the loop is late, never lost or
// misread.
//
// The contract an implementation keeps:
//
//   * takeEdge() hands over the oldest queued edge and returns true, or
//     returns false when none is queued. Edges come out in the order they
//     happened, down and up alternating.
//   * Each edge is stamped with when it happened (ButtonEdge::ms, on the
//     millis() clock), not with when takeEdge() is called.
//   * Edges are debounced: contact bounce is the implementation's to filter.
//
// Turning the hardware into edges is the implementation's job. A button that
// raises an interrupt, or one sampled from a timer, queues each edge from that
// context, and Sandbox drains the queue on the loop task, so the queue must be
// safe for one context writing while another reads (a single-producer,
// single-consumer ring, or a critical section around push and pop). A button
// that only gives a level is sampled on the implementation's side, in update()
// or a timer, emitting an edge on each debounced change, stamped at the sample
// that saw it. Sampled once per loop pass, edges are only as timely as the
// loop: a press shorter than an app callback can be missed entirely, so sample
// from a timer or take an interrupt where that matters.
class SystemButton : public Driver {
public:
  virtual bool takeEdge(ButtonEdge& edge) = 0;
};

} // namespace Resident

#endif // RESIDENT_SYSTEM_BUTTON_H
